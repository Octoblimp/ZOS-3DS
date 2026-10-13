#include "net/jpeg.h"

#include <cstring>
#include <memory>

namespace jpeg {

namespace {

// A coefficient's place in its block, by its index in zigzag order.
const u8 kNatural[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

// 12-bit fixed point.
constexpr int fix(double x)
{
    return (int)(x * 4096.0 + 0.5);
}

// Made before main: YCbCr to RGB's terms by Cb and Cr (16-bit fixed point
// for green's, rounded), and a clamp to 0..255 for -256..511.
struct Tables {
    int crR[256], cbB[256], cbG[256], crG[256];
    u8 clamp[768];
    Tables()
    {
        for (int i = 0; i < 256; i++) {
            const int c = i - 128;
            crR[i] = (c * 91881 + 32768) >> 16;
            cbB[i] = (c * 116130 + 32768) >> 16;
            cbG[i] = -c * 22554 + 32768;
            crG[i] = -c * 46802;
        }
        for (int i = 0; i < 768; i++)
            clamp[i] = (u8)(i < 256 ? 0 : i > 511 ? 255 : i - 256);
    }
};
const Tables s_tables;

inline u8 clamp255(int v)
{
    return (unsigned)v > 255 ? (v < 0 ? 0 : 255) : (u8)v;
}

struct Huffman {
    bool present = false;
    u8 values[256] = {};
    int maxcode[18] = {};  // the longest code of each length, -1 for none
    int valptr[17] = {};   // its first value's index
    int mincode[17] = {};
    // By the next 9 bits: the code's length << 8 | its value, or 0 when the
    // code is longer.
    u16 fast[512] = {};
    // An AC table's, by the next 9 bits when they hold a coefficient's code
    // and its extra bits both: its value << 8 | the zeros' run << 4 | the
    // bits taken. 0 otherwise (and for the end of block and 16 zeros).
    s16 fastAc[512] = {};
};

struct Component {
    int id = 0;
    int h = 1, v = 1;  // sampling factors
    int tq = 0;        // quantization table
    int td = 0, ta = 0; // the scan's DC and AC tables
    int dcPred = 0;
    int planeW = 0, planeH = 0;
    std::vector<u8> plane;
};

struct Decoder {
    u16 quant[4][64] = {};  // zigzag order
    bool quantPresent[4] = {};
    Huffman dc[4], ac[4];
    Component comps[3];
    int ncomp = 0;
    int width = 0, height = 0;
    int hmax = 1, vmax = 1;
    int mcux = 0, mcuy = 0;
    int restartInterval = 0;
    bool frame = false;
    bool scanned = false;
};

// The entropy-coded bits, MSB first, with the stuffed zero after each 0xFF
// dropped. At a marker it stops, leaving p on its 0xFF, and reads zeros.
struct Bits {
    const u8* p;
    const u8* end;
    u32 acc = 0;
    int count = 0;
    bool marker = false;

    void fill()
    {
        while (count <= 24) {
            u32 byte = 0;
            if (!marker && p < end) {
                byte = *p;
                if (byte == 0xFF) {
                    const u8 next = p + 1 < end ? p[1] : 0xD9;
                    if (next == 0x00) {
                        p += 2;
                    } else {
                        marker = true;
                        byte = 0;
                    }
                } else {
                    p++;
                }
            }
            acc |= byte << (24 - count);
            count += 8;
        }
    }
    void skip(int n)
    {
        acc <<= n;
        count -= n;
    }
};

int be16(const u8* p)
{
    return p[0] << 8 | p[1];
}

bool buildHuffman(Huffman& h, const u8* counts, const u8* symbols, int total)
{
    if (total > 256)
        return false;
    memcpy(h.values, symbols, total);
    memset(h.fast, 0, sizeof(h.fast));
    int code = 0, k = 0;
    for (int len = 1; len <= 16; len++) {
        const int n = counts[len - 1];
        if (code + n > (1 << len))
            return false; // more codes than the length holds
        h.valptr[len] = k;
        h.mincode[len] = code;
        if (len <= 9) {
            const int shift = 9 - len;
            for (int i = 0; i < n; i++)
                for (int fill = 0; fill < (1 << shift); fill++)
                    h.fast[(code + i) << shift | fill] = (u16)(len << 8 | symbols[k + i]);
        }
        code += n;
        k += n;
        h.maxcode[len] = n ? code - 1 : -1;
        code <<= 1;
    }
    h.maxcode[17] = 0x7fffffff;
    for (int i = 0; i < 512; i++) {
        h.fastAc[i] = 0;
        const u16 f = h.fast[i];
        if (!f)
            continue;
        const int len = f >> 8, rs = f & 0xff;
        const int run = rs >> 4, s = rs & 15;
        if (s == 0 || len + s > 9)
            continue;
        int v = (i << len) & 511;
        v >>= 9 - s;
        if (v < 1 << (s - 1))
            v -= (1 << s) - 1;
        h.fastAc[i] = (s16)(v * 256 + run * 16 + len + s);
    }
    h.present = true;
    return true;
}

// The next Huffman-coded value, or -1 for a code the table lacks.
int decodeSymbol(Bits& b, const Huffman& h)
{
    b.fill();
    const u16 f = h.fast[b.acc >> 23];
    if (f) {
        b.skip(f >> 8);
        return f & 0xff;
    }
    for (int len = 10; len <= 16; len++) {
        const int code = (int)(b.acc >> (32 - len));
        if (code <= h.maxcode[len]) {
            b.skip(len);
            return h.values[h.valptr[len] + code - h.mincode[len]];
        }
    }
    return -1;
}

// The next s bits as a signed value (F.2.2.1's RECEIVE and EXTEND).
int receiveExtend(Bits& b, int s)
{
    if (s == 0)
        return 0;
    b.fill();
    const int v = (int)(b.acc >> (32 - s));
    b.skip(s);
    return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
}

// A dequantized coefficient, kept to 16 bits as libjpeg's are (a real
// image's are well inside; a broken one's only make broken pixels).
inline int dequantize(int v, int q)
{
    v *= q;
    return v < -32768 ? -32768 : v > 32767 ? 32767 : v;
}

// One 8-point inverse DCT in 12-bit fixed point, Loeffler, Ligtenberg and
// Moschytz's factorization (libjpeg's "islow"). The results are x0 + t3,
// x1 + t2, x2 + t1, x3 + t0, x3 - t0, x2 - t1, x1 - t2, x0 - t3.
struct Idct8 {
    int x0, x1, x2, x3, t0, t1, t2, t3;
    Idct8(int s0, int s1, int s2, int s3, int s4, int s5, int s6, int s7)
    {
        // The even part.
        int p1 = (s2 + s6) * fix(0.5411961);
        t2 = p1 + s6 * fix(-1.847759065);
        t3 = p1 + s2 * fix(0.765366865);
        const int e0 = (s0 + s4) * 4096, e1 = (s0 - s4) * 4096;
        x0 = e0 + t3;
        x3 = e0 - t3;
        x1 = e1 + t2;
        x2 = e1 - t2;
        // The odd part.
        int p3 = s7 + s3, p4 = s5 + s1;
        p1 = s7 + s1;
        int p2 = s5 + s3;
        const int p5 = (p3 + p4) * fix(1.175875602);
        t0 = s7 * fix(0.298631336);
        t1 = s5 * fix(2.053119869);
        t2 = s3 * fix(3.072711026);
        t3 = s1 * fix(1.501321110);
        p1 = p5 + p1 * fix(-0.899976223);
        p2 = p5 + p2 * fix(-2.562915447);
        p3 *= fix(-1.961570560);
        p4 *= fix(-0.390180644);
        t3 += p1 + p4;
        t2 += p2 + p3;
        t1 += p2 + p4;
        t0 += p1 + p3;
    }
};

// The rows' rounding and shift from -128..127 to 0..255. The columns keep
// 2 bits more than the samples; with the 12 bits of each pass and the
// sqrt(8) of each, the rows take off 17.
const int kRowBias = 65536 + (128 << 17);

// A block's dequantized coefficients (natural order) to 8 x 8 samples.
void idct(const int* in, u8* out, int stride)
{
    int tmp[64];
    for (int i = 0; i < 8; i++) {
        const int* d = in + i;
        int* v = tmp + i;
        if (!(d[8] | d[16] | d[24] | d[32] | d[40] | d[48] | d[56])) {
            const int dc = d[0] * 4;
            v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dc;
            continue;
        }
        const Idct8 t(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56]);
        const int x0 = t.x0 + 512, x1 = t.x1 + 512, x2 = t.x2 + 512, x3 = t.x3 + 512;
        v[0] = (x0 + t.t3) >> 10;
        v[56] = (x0 - t.t3) >> 10;
        v[8] = (x1 + t.t2) >> 10;
        v[48] = (x1 - t.t2) >> 10;
        v[16] = (x2 + t.t1) >> 10;
        v[40] = (x2 - t.t1) >> 10;
        v[24] = (x3 + t.t0) >> 10;
        v[32] = (x3 - t.t0) >> 10;
    }
    for (int i = 0; i < 8; i++, out += stride) {
        const int* v = tmp + i * 8;
        const Idct8 t(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        const int x0 = t.x0 + kRowBias, x1 = t.x1 + kRowBias, x2 = t.x2 + kRowBias, x3 = t.x3 + kRowBias;
        out[0] = clamp255((x0 + t.t3) >> 17);
        out[7] = clamp255((x0 - t.t3) >> 17);
        out[1] = clamp255((x1 + t.t2) >> 17);
        out[6] = clamp255((x1 - t.t2) >> 17);
        out[2] = clamp255((x2 + t.t1) >> 17);
        out[5] = clamp255((x2 - t.t1) >> 17);
        out[3] = clamp255((x3 + t.t0) >> 17);
        out[4] = clamp255((x3 - t.t0) >> 17);
    }
}

bool decodeBlock(Bits& b, Component& c, const Huffman& dc, const Huffman& ac, const u16* q, u8* out, int stride)
{
    const int t = decodeSymbol(b, dc);
    if (t < 0 || t > 11)
        return false;
    c.dcPred += receiveExtend(b, t);
    int coef[64];
    memset(coef, 0, sizeof(coef));
    coef[0] = dequantize(c.dcPred, q[0]);
    bool acZero = true;
    for (int k = 1; k < 64;) {
        b.fill();
        const int fast = ac.fastAc[b.acc >> 23];
        if (fast) {
            b.skip(fast & 15);
            k += (fast >> 4) & 15;
            if (k > 63)
                return false;
            coef[kNatural[k]] = dequantize(fast >> 8, q[k]);
            acZero = false;
            k++;
            continue;
        }
        const int rs = decodeSymbol(b, ac);
        if (rs < 0)
            return false;
        const int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r != 15)
                break; // end of block
            k += 16;
            continue;
        }
        k += r;
        if (k > 63)
            return false;
        coef[kNatural[k]] = dequantize(receiveExtend(b, s), q[k]);
        acZero = false;
        k++;
    }
    if (acZero) {
        // Flat (most of a tile's sky, water and roofs): the DC alone.
        const u8 v = clamp255((coef[0] * 16384 + kRowBias) >> 17);
        for (int y = 0; y < 8; y++, out += stride)
            memset(out, v, 8);
        return true;
    }
    idct(coef, out, stride);
    return true;
}

bool decodeScan(Decoder& d, const u8* data, size_t size, size_t& pos, std::string& error)
{
    // pos is on the segment's length.
    const u8* seg = data + pos;
    const int len = be16(seg);
    const int ns = seg[2];
    if (ns < 1 || ns > d.ncomp || len < 6 + 2 * ns) {
        error = "bad scan";
        return false;
    }
    int scan[3];
    for (int i = 0; i < ns; i++) {
        const int id = seg[3 + 2 * i];
        const int tables = seg[4 + 2 * i];
        int found = -1;
        for (int c = 0; c < d.ncomp; c++)
            if (d.comps[c].id == id)
                found = c;
        if (found < 0) {
            error = "scan of an unknown component";
            return false;
        }
        Component& c = d.comps[found];
        c.td = tables >> 4;
        c.ta = tables & 15;
        if (c.td > 3 || c.ta > 3 || !d.dc[c.td].present || !d.ac[c.ta].present || !d.quantPresent[c.tq]) {
            error = "missing table";
            return false;
        }
        scan[i] = found;
    }
    pos += len;
    for (int c = 0; c < d.ncomp; c++)
        d.comps[c].dcPred = 0;

    Bits b;
    b.p = data + pos;
    b.end = data + size;
    int mcus = 0;
    // At a restart interval's end: the RSTn marker, then a fresh start.
    auto restart = [&]() {
        const u8* q = b.p;
        while (q + 1 < b.end && !(q[0] == 0xFF && q[1] >= 0xD0 && q[1] <= 0xD7))
            q++;
        if (q + 1 >= b.end)
            return false;
        b.p = q + 2;
        b.acc = 0;
        b.count = 0;
        b.marker = false;
        for (int c = 0; c < d.ncomp; c++)
            d.comps[c].dcPred = 0;
        return true;
    };
    auto atRestart = [&]() { return d.restartInterval && mcus && mcus % d.restartInterval == 0; };

    if (ns == 1) {
        // Not interleaved: the component's blocks in raster order, one an MCU.
        Component& c = d.comps[scan[0]];
        const int cw = (d.width * c.h + d.hmax - 1) / d.hmax;
        const int ch = (d.height * c.v + d.vmax - 1) / d.vmax;
        const int bw = (cw + 7) / 8, bh = (ch + 7) / 8;
        for (int by = 0; by < bh; by++) {
            for (int bx = 0; bx < bw; bx++, mcus++) {
                if (atRestart() && !restart())
                    goto bad;
                if (!decodeBlock(b, c, d.dc[c.td], d.ac[c.ta], d.quant[c.tq],
                                 &c.plane[(size_t)by * 8 * c.planeW + bx * 8], c.planeW))
                    goto bad;
            }
        }
    } else {
        for (int my = 0; my < d.mcuy; my++) {
            for (int mx = 0; mx < d.mcux; mx++, mcus++) {
                if (atRestart() && !restart())
                    goto bad;
                for (int i = 0; i < ns; i++) {
                    Component& c = d.comps[scan[i]];
                    for (int v = 0; v < c.v; v++) {
                        for (int h = 0; h < c.h; h++) {
                            const size_t y = (size_t)(my * c.v + v) * 8, x = (size_t)(mx * c.h + h) * 8;
                            if (!decodeBlock(b, c, d.dc[c.td], d.ac[c.ta], d.quant[c.tq],
                                             &c.plane[y * c.planeW + x], c.planeW))
                                goto bad;
                        }
                    }
                }
            }
        }
    }

    // On to the marker after the scan's data (restart markers are inside).
    pos = b.p - data;
    while (pos + 1 < size &&
           !(data[pos] == 0xFF && data[pos + 1] != 0x00 && data[pos + 1] != 0xFF &&
             !(data[pos + 1] >= 0xD0 && data[pos + 1] <= 0xD7)))
        pos++;
    d.scanned = true;
    return true;

bad:
    error = "bad image data";
    return false;
}

bool readFrame(Decoder& d, const u8* body, int len, std::string& error)
{
    if (len < 6 || body[0] != 8) {
        error = "not 8 bits";
        return false;
    }
    d.height = be16(body + 1);
    d.width = be16(body + 3);
    d.ncomp = body[5];
    if (d.ncomp != 1 && d.ncomp != 3) {
        error = "neither grey nor YCbCr";
        return false;
    }
    if (len < 6 + 3 * d.ncomp) {
        error = "bad frame";
        return false;
    }
    if (d.width <= 0 || d.height <= 0 || d.width > 4096 || d.height > 4096) {
        error = "bad size";
        return false;
    }
    d.hmax = d.vmax = 1;
    for (int i = 0; i < d.ncomp; i++) {
        Component& c = d.comps[i];
        c.id = body[6 + 3 * i];
        c.h = body[7 + 3 * i] >> 4;
        c.v = body[7 + 3 * i] & 15;
        c.tq = body[8 + 3 * i];
        if (c.h < 1 || c.h > 4 || c.v < 1 || c.v > 4 || c.tq > 3) {
            error = "bad component";
            return false;
        }
        d.hmax = c.h > d.hmax ? c.h : d.hmax;
        d.vmax = c.v > d.vmax ? c.v : d.vmax;
    }
    d.mcux = (d.width + 8 * d.hmax - 1) / (8 * d.hmax);
    d.mcuy = (d.height + 8 * d.vmax - 1) / (8 * d.vmax);
    for (int i = 0; i < d.ncomp; i++) {
        Component& c = d.comps[i];
        c.planeW = d.mcux * c.h * 8;
        c.planeH = d.mcuy * c.v * 8;
        c.plane.assign((size_t)c.planeW * c.planeH, 0);
    }
    d.frame = true;
    return true;
}

// The markers to the end of the scan, into d's planes.
bool parse(Decoder& d, const u8* data, size_t size, std::string& error)
{
    if (!isJpeg(data, size)) {
        error = "not a JPEG";
        return false;
    }
    size_t pos = 2;
    for (;;) {
        while (pos < size && data[pos] != 0xFF)
            pos++;
        while (pos < size && data[pos] == 0xFF)
            pos++;
        if (pos >= size) {
            if (d.scanned)
                break; // no EOI: what was decoded will do
            error = "truncated";
            return false;
        }
        const u8 m = data[pos++];
        if (m == 0xD9)
            break; // EOI
        if ((m >= 0xD0 && m <= 0xD7) || m == 0x01)
            continue; // no segment
        if (pos + 2 > size) {
            error = "truncated";
            return false;
        }
        const int len = be16(data + pos);
        if (len < 2 || pos + len > size) {
            error = "truncated segment";
            return false;
        }
        const u8* body = data + pos + 2;
        const int blen = len - 2;
        switch (m) {
        case 0xDB: { // DQT
            int i = 0;
            while (i < blen) {
                const int pq = body[i] >> 4, tq = body[i] & 15;
                i++;
                if (tq > 3 || i + 64 * (pq ? 2 : 1) > blen) {
                    error = "bad quantization table";
                    return false;
                }
                for (int k = 0; k < 64; k++) {
                    d.quant[tq][k] = pq ? (u16)be16(body + i) : body[i];
                    i += pq ? 2 : 1;
                }
                d.quantPresent[tq] = true;
            }
            break;
        }
        case 0xC4: { // DHT
            int i = 0;
            while (i < blen) {
                if (i + 17 > blen) {
                    error = "bad Huffman table";
                    return false;
                }
                const int tc = body[i] >> 4, th = body[i] & 15;
                const u8* counts = body + i + 1;
                int total = 0;
                for (int k = 0; k < 16; k++)
                    total += counts[k];
                if (tc > 1 || th > 3 || i + 17 + total > blen ||
                    !buildHuffman(tc == 0 ? d.dc[th] : d.ac[th], counts, body + i + 17, total)) {
                    error = "bad Huffman table";
                    return false;
                }
                i += 17 + total;
            }
            break;
        }
        case 0xC0: // baseline
        case 0xC1: // extended sequential
            if (!readFrame(d, body, blen, error))
                return false;
            break;
        case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7:
        case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
            error = "progressive, lossless or arithmetic-coded";
            return false;
        case 0xDD: // DRI
            if (blen < 2) {
                error = "bad restart interval";
                return false;
            }
            d.restartInterval = be16(body);
            break;
        case 0xDA: // SOS
            if (!d.frame) {
                error = "scan before frame";
                return false;
            }
            if (!decodeScan(d, data, size, pos, error))
                return false;
            continue; // pos is on the next marker
        default:
            break; // APPn, COM and the rest
        }
        pos += len;
    }
    if (!d.scanned) {
        error = "no scan";
        return false;
    }
    return true;
}

// The planes upsampled (nearest) and to RGB, a row at a time: out.row(y),
// then out.pixel(x, r, g, b) along it.
template <typename Out>
void convert(const Decoder& d, Out& out)
{
    // Each component's sample for each x, worked out once rather than
    // divided for every pixel (the ARM11 has no divide).
    std::vector<u16> cols[3];
    for (int i = 0; i < d.ncomp; i++) {
        cols[i].resize(d.width);
        for (int x = 0; x < d.width; x++)
            cols[i][x] = (u16)(x * d.comps[i].h / d.hmax);
    }
    const Component& c0 = d.comps[0];
    const u16* x0 = cols[0].data();
    const u8* clamp = s_tables.clamp + 256;
    for (int y = 0; y < d.height; y++) {
        out.row(y);
        const u8* yRow = &c0.plane[(size_t)(y * c0.v / d.vmax) * c0.planeW];
        if (d.ncomp == 1) {
            for (int x = 0; x < d.width; x++) {
                const int v = yRow[x0[x]];
                out.pixel(x, v, v, v);
            }
            continue;
        }
        const Component& c1 = d.comps[1];
        const Component& c2 = d.comps[2];
        const u8* cbRow = &c1.plane[(size_t)(y * c1.v / d.vmax) * c1.planeW];
        const u8* crRow = &c2.plane[(size_t)(y * c2.v / d.vmax) * c2.planeW];
        const u16* x1 = cols[1].data();
        const u16* x2 = cols[2].data();
        for (int x = 0; x < d.width; x++) {
            const int Y = yRow[x0[x]];
            const int cb = cbRow[x1[x]];
            const int cr = crRow[x2[x]];
            out.pixel(x, clamp[Y + s_tables.crR[cr]], clamp[Y + ((s_tables.cbG[cb] + s_tables.crG[cr]) >> 16)],
                      clamp[Y + s_tables.cbB[cb]]);
        }
    }
}

struct RgbaOut {
    u8* data;
    int width;
    u8* p = nullptr;
    void row(int y) { p = data + (size_t)y * width * 4; }
    void pixel(int x, int r, int g, int b)
    {
        u8* o = p + x * 4;
        o[0] = (u8)r;
        o[1] = (u8)g;
        o[2] = (u8)b;
        o[3] = 255;
    }
};

// Morton order within an 8 x 8 tile: x's bits and y's, interleaved.
const u8 kMortonX[8] = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15};
const u8 kMortonY[8] = {0x00, 0x02, 0x08, 0x0a, 0x20, 0x22, 0x28, 0x2a};

struct TiledRgb565Out {
    u16* data;
    int width;
    std::vector<u32> cols; // a pixel's place in its row of tiles, by x
    u16* p = nullptr;
    TiledRgb565Out(u16* data, int width) : data(data), width(width), cols(width)
    {
        for (int x = 0; x < width; x++)
            cols[x] = (u32)((x >> 3) * 64 + kMortonX[x & 7]);
    }
    void row(int y) { p = data + (size_t)(y >> 3) * width * 8 + kMortonY[y & 7]; }
    void pixel(int x, int r, int g, int b) { p[cols[x]] = (u16)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
};

} // namespace

bool isJpeg(const u8* data, size_t size)
{
    return size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

bool decode(const u8* data, size_t size, std::vector<u8>& rgba, int& width, int& height, std::string& error)
{
    rgba.clear();
    width = height = 0;
    // A few KB of tables: off the stack, which is small on the loaders'
    // threads.
    std::unique_ptr<Decoder> d(new Decoder());
    if (!parse(*d, data, size, error))
        return false;
    rgba.resize((size_t)d->width * d->height * 4);
    RgbaOut out{rgba.data(), d->width};
    convert(*d, out);
    width = d->width;
    height = d->height;
    return true;
}

bool decodeTiledRGB565(const u8* data, size_t size, std::vector<u16>& pixels, int& width, int& height,
                       std::string& error)
{
    pixels.clear();
    width = height = 0;
    std::unique_ptr<Decoder> d(new Decoder());
    if (!parse(*d, data, size, error))
        return false;
    if ((d->width | d->height) & 7) {
        error = "sides not multiples of 8";
        return false;
    }
    pixels.resize((size_t)d->width * d->height);
    TiledRgb565Out out(pixels.data(), d->width);
    convert(*d, out);
    width = d->width;
    height = d->height;
    return true;
}

} // namespace jpeg
