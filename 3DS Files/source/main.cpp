// AppDelegate's set-up and HomeScene, which loads GameNode behind its
// loading bar and goes back to a saved game, or shows Play. The game is on
// the bottom screen, its stats on the top.
//
//   stylus: the menus, and a drag pans the map
//   circle pad / D-pad: pan; L/R: zoom (GameNode's)
//   B: cancel the action; Y: evacuate the waiting helicopter (GUIScene's)
//   SELECT: positional sound on or off (GUIScene's, kept in Prefs)
//   START: save and quit

#include <3ds.h>
#include <malloc.h>

#include <unistd.h>

#include <cstdio>
#include <stdexcept>
#include <string>

#include "cocos/director.h"
#include "cocos/simpleaudioengine.h"
#include "debuglog.h"
#include "heapcfg.h"
#include "paths.h"
#include "game/gamenode.h"
#include "game/homescene.h"
#include "game/maprenderer.h"
#include "game/placesearch.h"
#include "game/playermaps.h"
#include "game/satellitetileloader.h"
#include "game/simulation.h"
#include "net/http.h"

using namespace cocos2d;
using CocosDenshion::SimpleAudioEngine;

// The app's memory is split by heapcfg.c, before main(): libctru's own split
// stops the linear heap at 32 MB, and the textures and sounds need more
// (heapcfg.c), but one linear block of the whole New 3DS grant is refused
// on hardware.

namespace {

// The threads that use the network, stopped before anything they use goes:
// after the exit libctru closes the card under any thread still running, and
// 1.0.0 crashed at power-off with a tile download still writing one. The
// network itself goes only if they all stopped.
void stopNetworkThreads()
{
    const bool tiles = SatelliteTileLoader::shutdown();
    const bool search = PlaceSearch::shutdown();
    const bool maps = PlayerMaps::shutdown();
    if (tiles && search && maps)
        net::exit();
    dbg("network threads stopped%s", tiles && search && maps ? "" : " (not all: the network is left up)");
}

// Saving with no map up removes the save, so not while loading.
void saveGame(GameNode* game, const char* why)
{
    if (!game->finishedLoading())
        return;
    game->applicationDidEnterBackground();
    dbg("saved (%s)", why);
}

// AppDelegate::applicationDidEnterBackground: the game saved whenever
// Android put it in the background. Here that is HOME and sleep (the lid
// closing): the app may never come back from either (closed from the HOME
// Menu, the battery running out). libctru calls both hooks from
// aptMainLoop on this thread, between frames, so the game is never caught
// mid-step. A close from the HOME Menu also leaves the main loop, which
// saves again.
//
// It paused the music there too, and applicationWillEnterForeground resumed
// it (ndsp also stops while the app is away, and comes back by itself).
void onApt(APT_HookType hook, void* param)
{
    if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP) {
        SimpleAudioEngine::getInstance()->pauseBackgroundMusic();
        saveGame(static_cast<GameNode*>(param), hook == APTHOOK_ONSUSPEND ? "HOME" : "sleep");
    } else if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) {
        SimpleAudioEngine::getInstance()->resumeBackgroundMusic();
    }
}

// tools/prepare_data.py makes the game's data from your own APK. The
// default build carries it in its RomFS; a -NoData build reads it from the
// card as sdmc:/ZombieOutbreakSim/. Without it the first texture fails and
// the app ends with nothing on screen (on a New 3DS, a restart to HOME: the
// 178 MB mode is undone). One file from each folder the start needs, so a
// partial copy shows too.
const char* const kRequiredData[] = {
    "images/GUI/MDPI/HomeScreen.t3x",
    "images/GUI/MDPI/GUI.t3x",
    "images/Fullscreen/512/ActionOverlay_Soldiers.t3x",
    "images/animations/girl01.t3x",
    "images/maps/map01/tile_16_00_00.t3x",
    "fonts/Visitor TT2 BRK.bcfnt",
    "audio/music/music01.wav",
    "data/cacert.pem",
};

// The first required file missing, or null.
const char* missingData()
{
    for (const char* rel : kRequiredData) {
        const std::string path = std::string(zosAssetRoot()) + "/" + rel;
        if (access(path.c_str(), F_OK) != 0)
            return rel;
    }
    return nullptr;
}

// Says so on the bottom screen in libctru's console, which needs no files,
// until START.
void showMissingData(const char* rel)
{
    consoleInit(GFX_BOTTOM, nullptr);
    printf("\n Zombie Outbreak Simulator\n\n");
    printf(" This build carries no game data,\n and it is not on the SD card.\n Missing:\n\n  %s/\n  %s\n\n", ZOS_DATA_ROOT, rel);
    printf(" On the PC, in the project:\n\n  python tools/prepare_data.py\n\n");
    printf(" then copy the folder\n\n  out/ZombieOutbreakSim\n\n");
    printf(" to the root of the SD card,\n so it is sdmc:/ZombieOutbreakSim/.\n");
    printf(" Or install a build with the\n data in it (build_cia.ps1).\n\n");
    printf(" Press START to quit.");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

} // namespace

int main(int argc, char* argv[])
{
    gfxInitDefault();
    // The New 3DS's 804 MHz and L2 cache, which the exheader only permits
    // (libctru puts it back after HOME and sleep). Nothing on an Old 3DS.
    osSetSpeedupEnable(true);
    dbg_open();
    dbg("ZombieOutbreakSim-3DS");
    dbg("app memory %lu KB, grant %lu KB, %lu KB committed: heap %lu KB (%lu tries), linear heap %lu KB of %lu KB asked (%lu tries, "
        "last refusal %08lx)%s",
        (unsigned long)(osGetMemRegionSize(MEMREGION_APPLICATION) / 1024), (unsigned long)(g_heapInfo.commitMax / 1024),
        (unsigned long)(g_heapInfo.commitUsed / 1024), (unsigned long)(envGetHeapSize() / 1024), (unsigned long)g_heapInfo.heapTries,
        (unsigned long)(envGetLinearHeapSize() / 1024), (unsigned long)(g_heapInfo.linearAsked / 1024), (unsigned long)g_heapInfo.linearTries,
        (unsigned long)g_heapInfo.linearFailRc, g_heapInfo.queryFailed ? ", limit query failed" : "");
    {
        // The PICA reaches vertex buffers from 0x18000000 with 28-bit
        // offsets, so below 0x28000000 (the New 3DS's second FCRAM bank).
        extern u32 __ctru_linear_heap;
        const u32 pa = osConvertVirtToPhys((const void*)__ctru_linear_heap);
        dbg("linear heap: physical %08lx to %08lx", (unsigned long)pa, (unsigned long)(pa + envGetLinearHeapSize()));
    }
    // The data from the build's RomFS if it has one (romfsInit fails
    // without), else from the card.
    if (R_SUCCEEDED(romfsInit())) {
        if (access("romfs:/images/GUI/MDPI/HomeScreen.t3x", F_OK) == 0)
            zosSetAssetRoot("romfs:");
        else
            romfsExit();
    }
    dbg("game data from %s", zosAssetRoot());
    if (const char* missing = missingData()) {
        dbg("data missing: %s/%s", zosAssetRoot(), missing);
        showMissingData(missing);
        dbg_close();
        gfxExit();
        return 1;
    }
    if (!gfx::init()) {
        dbg("gfx init failed");
        dbg_close();
        gfxExit();
        return 1;
    }

    // AppDelegate::applicationDidFinishLaunching: the MDPI GUI, the 512
    // full-screen images, the home screen's atlas, the grey clear colour.
    FileUtils* fu = FileUtils::getInstance();
    fu->addSearchPath("images/GUI/MDPI/");
    fu->addSearchPath("images/Fullscreen/512/");
    SpriteFrameCache::getInstance()->addSpriteFramesWithFile("HomeScreen.plist");
    Director* director = Director::getInstance();
    director->setClearColor(Color4F(0.5f, 0.5f, 0.5f, 1.0f));

    dbg("linear free %lu KB before the game", (unsigned long)(linearSpaceFree() / 1024));
    GameNode* game = nullptr;
    aptHookCookie aptCookie;
    try {
        game = GameNode::create(1.0f, director->getVisibleSize(), fu->getWritablePath());
        game->retain();
        aptHook(&aptCookie, onApt, game);
        director->runWithScene(HomeScene::createScene(game));

        // The music, looped, as AppDelegate started it.
        SimpleAudioEngine* audio = SimpleAudioEngine::getInstance();
        if (audio->willPlayBackgroundMusic()) {
            audio->setBackgroundMusicVolume(1.0f);
            audio->playBackgroundMusic("audio/music/music01.mp3", true);
        }

        dbg("entering main loop");
        const u64 t0 = osGetTime();
        bool loaded = false;
        int frames = 0;
        MapRenderer::OnlineStats lastOnline;
        int lastConnections = 0;
        while (director->mainLoop()) {
            const u32 down = director->buttonsDown();
            if (down & KEY_START)
                break;
            if (!loaded && game->finishedLoading()) {
                loaded = true;
                dbg("game loaded in %d ms, linear free %lu KB", (int)(osGetTime() - t0),
                    (unsigned long)(linearSpaceFree() / 1024));
            }
            if (++frames % 300 == 0) {
                Simulation::Stats st;
                if (game->getLastSimulationStats(st))
                    dbg("sim %5.1f s: civs %d zombies %d dead %d level %d, step %.2f ms, zoom %.2f, %d tiles, "
                        "linear free %lu KB, heap used %d KB, sounds %d, music at %u",
                        st.gameTime / 1000.0f, st.numCivs, st.numZombies, st.numDead, st.outbreakLevel,
                        st.stepDuration * 1000.0, game->getMapScale(), game->getNumTilesLoaded(),
                        (unsigned long)(linearSpaceFree() / 1024), mallinfo().uordblks / 1024,
                        SimpleAudioEngine::getInstance()->effectsPlaying(),
                        SimpleAudioEngine::getInstance()->musicSamplePosition());
                const Director::FrameProfile fp = director->takeFrameProfile();
                const gfx::Stats gs = gfx::lastFrameStats();
                dbg("frame %.2f ms (worst %.2f): input %.2f, update %.2f, top %.2f, bottom %.2f, submit %.2f, wait %.2f; "
                    "gpu %.2f; %d draws, %d vertices",
                    fp.average, fp.worst, fp.input, fp.update, fp.top, fp.bottom, fp.submit, fp.wait, fp.gpu, gs.draws,
                    gs.vertices);
                // The satellite tiles' traffic since the last line, if any.
                const MapRenderer::OnlineStats os = game->getMapRenderer()->getOnlineStats();
                const int connections = net::connectionsMade();
                if (os.asked != lastOnline.asked)
                    dbg("imagery: %d asked, %d looked for among the kept, %d to download, %d dropped, %d downloaded; "
                        "%d connections made",
                        os.asked - lastOnline.asked, os.cardReads - lastOnline.cardReads, os.sent - lastOnline.sent,
                        os.dropped - lastOnline.dropped, os.downloaded - lastOnline.downloaded,
                        connections - lastConnections);
                lastOnline = os;
                lastConnections = connections;
            }
        }
    } catch (const std::exception& e) {
        dbg("game: %s", e.what());
        if (game)
            aptUnhook(&aptCookie);
        stopNetworkThreads();
        SimpleAudioEngine::end();
        dbg_close();
        gfx::exit();
        gfxExit();
        return 1;
    }
    dbg("left main loop");
    aptUnhook(&aptCookie);
    saveGame(game, "exit");
    stopNetworkThreads();

    director->setTopScreenNode(nullptr);
    game->release();
    SimpleAudioEngine::end();
    gfx::exit();
    dbg_close();
    gfxExit();
    return 0;
}
