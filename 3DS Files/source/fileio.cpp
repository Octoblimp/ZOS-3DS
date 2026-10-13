#include "fileio.h"

#include "paths.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

bool readWholeFile(const char* path, std::vector<u8>& data)
{
    data.clear();
    const int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    struct stat st;
    bool ok = fstat(fd, &st) == 0;
    if (ok && st.st_size > 0) {
        data.resize((size_t)st.st_size);
        size_t got = 0;
        while (got < data.size()) {
            const ssize_t n = read(fd, data.data() + got, data.size() - got);
            if (n <= 0)
                break;
            got += (size_t)n;
        }
        ok = got == data.size();
        if (!ok)
            data.clear();
    }
    close(fd);
    return ok;
}

static const char* s_assetRoot = ZOS_DATA_ROOT;

const char* zosAssetRoot()
{
    return s_assetRoot;
}

void zosSetAssetRoot(const char* root)
{
    s_assetRoot = root;
}
