// ECHO SHIFT: The Parallel Dimension
// Step 9 - Final build: Puzzle Lock (Math / Logic / Tech) + Score + Grades + Graduation
//   Some crystals are LOCKED (orange cage): stand next to one and press F to
//   solve a puzzle (Math, Logic or Tech; Easy / Medium / Hard).
//   Solving puzzles gives SCORE. Score raises your GRADE:
//   Freshman -> Sophomore -> Junior -> Senior -> GRADUATE (game ends with graduation).
//
// Build (classic GLUT, same linker libs as before):
//   g++ main.cpp -o echoshift.exe -lglut32 -lglu32 -lopengl32
//   Code::Blocks linker order: glut32, glu32, opengl32
//
// Controls:
//   W A S D            : move (relative to camera)
//   SPACE              : jump
//   E                  : switch dimension (Reality <-> Echo)
//   Q                  : create an Echo clone (replays your last 5 seconds)
//   Mouse drag / Arrows: rotate camera
//   + / -              : zoom
//   R                  : respawn at last checkpoint
//   ESC                : pause (in menus: back / quit)
//   F                  : unlock a caged crystal (opens the puzzle)
//   Typed puzzle       : type the answer, ENTER = submit, BACKSPACE = delete, ESC = cancel
//   Multiple choice    : press 1 - 4 to choose an option
//   1 / 2 / 3          : jump to level 1 / 2 / 3
//   ENTER              : continue after a level is complete

#define GLUT_DISABLE_ATEXIT_HACK
#include <windows.h>
#include <GL/glut.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------- data
struct Island {
    float x, y, z;
    float sx, sz;
    float phase;
    float r, g, b;
};

// A bridge only exists in ONE dimension (0 = Reality, 1 = Echo).
struct Bridge {
    float x, y, z;      // y = walkable top surface
    float sx, sz;
    int   dim;
    float fade;
};

struct Crystal {
    float x, y, z;
    float r, g, b;
    float phase;
    bool  collected;
    bool  locked;        // needs a puzzle (press F next to it)
    float lockTimer;     // lockout after too many wrong answers
};

// Horizontal laser beam between two emitters. dim = -1 means both dimensions.
struct Laser {
    float x1, z1, x2, z2, y;
    int   dim;
    float phase, period, onTime;
    float fade;
    bool  on, warn;
};

// Pressure plate sitting on an island; keeps its door open for 'hold' seconds.
struct Plate {
    int   island;
    float ox, oz;
    int   door;
    float hold, timer, press;
    bool  active;
};

// Door = box that slides up when opened.
struct Door {
    float x, y, z;      // y = base
    float sx, sz, h;
    int   dim;
    float open, fade, target;
    int   needed, count;     // plates needed / currently pressed
};

struct Checkpoint {
    int   island;
    float ox, oz;
    bool  active;
};

struct Particle {
    float x, y, z, vx, vy, vz, life;
    float r, g, b;
};

struct Star { float x, y, z; };
struct Solid { float x, z, sx, sz, top, thick; };

static std::vector<Island>     islands;
static std::vector<Bridge>     bridges;
static std::vector<Crystal>    crystals;
static std::vector<Laser>      lasers;
static std::vector<Plate>      plates;
static std::vector<Door>       doors;
static std::vector<Checkpoint> checkpoints;
static std::vector<Particle>   particles;
static std::vector<Star>       stars;
static std::vector<Solid>      solids;

// ---- Echo clone: we always record the last few seconds of movement;
//      pressing Q turns that recording into a ghost that replays it once
//      and then stays where the recording ended.
struct Sample {
    float t, x, y, z;
    float heading, speed, walk;
    bool  grounded;
};
static const float ECHO_WINDOW = 5.0f;    // seconds of movement remembered
static const float GHOST_SPEED = 1.5f;    // replay speed multiplier
static const float GHOST_HOLD  = 15.0f;   // seconds the clone stays after replaying

static std::deque<Sample>  recBuf;
static std::vector<Sample> ghostTrack;
static bool   ghostActive = false, ghostDone = false;
static float  ghostClock = 0.0f, ghostHold = 0.0f, ghostAge = 0.0f;
static size_t ghostIdx = 0;
static float  gx = 0, gy = 0, gz = 0, gHead = 0, gSpeed = 0, gWalk = 0;
static bool   gGround = false;
static float  echoCooldown = 0.0f;

// ---- player constants
static const float PLAYER_R   = 0.40f;
static const float PLAYER_H   = 2.00f;
static const float MOVE_SPEED = 7.0f;
static const float JUMP_V     = 10.5f;
static const float GRAVITY    = 24.0f;
static const float FALL_LIMIT = -25.0f;
static const float MAX_HEALTH = 100.0f;
static const float LASER_DMG  = 25.0f;

// ---- player state
static float px = 0, py = 0, pz = 0;
static float vx = 0, vy = 0, vz = 0;
static float heading = 0.0f;
static float walkPhase = 0.0f;
static bool  grounded = false;
static float coyote = 0.0f;
static bool  keys[256];

// ---- game state
static float health = MAX_HEALTH;
static float invuln = 0.0f;
static float damageFlash = 0.0f;
static int   crystalsGot = 0;
static int   falls = 0;
static int   deaths = 0;
static int   curCP = 0;
static char  msgText[96] = "";
static float msgTimer = 0.0f;

// ---- level / game state
enum GameState { STATE_MENU, STATE_PLAYING, STATE_PAUSED, STATE_LEVEL_DONE, STATE_VICTORY, STATE_HOWTO, STATE_PUZZLE, STATE_GRADUATED };
static const int NUM_LEVELS = 3;
static GameState gameState = STATE_MENU;
static int   curLevel = 0;
static float levelTime = 0.0f;
static int   levelNeeded = 0;
static float bannerTimer = 0.0f;
static float skyReal[3] = { 0.03f, 0.05f, 0.18f };
static float skyEcho[3] = { 0.15f, 0.03f, 0.24f };
static int   totalCrystals = 0, totalFalls = 0, totalDefeats = 0;
static float totalTime = 0.0f;
static int   puzzlesSolved = 0, puzzlesWrong = 0, totalPuzzles = 0, totalWrong = 0;

// ---- crystal puzzle lock
static int   puzzleCrystal = -1;       // index of the crystal being unlocked
static int   nearCrystal = -1;         // locked crystal close enough to press F
static int   puzzleType = 0;
static int   puzzleAnswer = 0;
static char  puzzleQuestion[128] = "";
static char  puzzleHint[96] = "";
static char  puzzleInput[10] = "";
static int   puzzleLen = 0;
static int   puzzleAttempts = 0;       // wrong answers so far for this crystal
static int   puzzleResult = 0;         // 0 = solving, 1 = unlocked, 2 = locked out
static float puzzleTimer = 0.0f;       // length of the result animation
static float puzzleCooldown = 0.0f;    // stops the puzzle from re-opening instantly
static const int PUZZLE_MAX_TRIES = 3;

static int   puzzleDiff = 0;           // 0 easy, 1 medium, 2 hard
static int   puzzleCat = 0;            // 0 math, 1 logic, 2 tech
static bool  puzzleMC = false;         // multiple-choice?
static char  puzzleOpt[4][44];
static bool  puzzleOptOff[4] = { false, false, false, false };
static int   puzzleCorrect = 0;
static int   puzzleEarned = 0;         // points for the current puzzle
static int   puzzleStreak = 0;         // first-try streak
static const char* catNames[3]  = { "MATH", "LOGIC", "TECH" };
static const char* diffNames[3] = { "EASY", "MEDIUM", "HARD" };
static const int   diffBase[3]  = { 10, 20, 30 };

// ---- score & grade
static int   score = 0, levelStartScore = 0, levelBonus = 0;
static float promoTimer = 0.0f;
static char  promoText[64] = "";
static float graduateDelay = -1.0f;
static const int NUM_GRADES = 5;
static const char* gradeNames[NUM_GRADES] = { "Freshman", "Sophomore", "Junior", "Senior", "GRADUATE" };
static const int   gradeNeed[NUM_GRADES]  = { 0, 80, 200, 350, 500 };   // tune these to change difficulty

struct Portal { int island; float ox, oz; bool open; };
static Portal portal = { 0, 0.0f, 0.0f, false };
static bool   portalReady = false;

// ---- Step 7: effects + menu state
static float camShake = 0.0f;          // screen shake strength
static float fadeAlpha = 1.0f;         // fade-in from black
static float ripple = -1.0f;           // dimension-switch shockwave 0..1 (-1 = off)
static float squash = 0.0f;            // + squashed, - stretched
static float stepTimer = 0.0f;
static float crystalPulse = 0.0f;
static float gEyeOpen = 1.0f;          // blinking
static float camDistView = 30.0f;      // smoothed camera distance
static int   menuIndex = 0;
static int   selectedLevel = 0;
static float mapMinX = -20, mapMaxX = 20, mapMinZ = -20, mapMaxZ = 20;
static float menuFx = 0, menuFy = 2, menuFz = -5, menuDist = 34;
static const int NUM_MOTES = 90;
static float motes[NUM_MOTES][4];

// ---- dimension state
static int   dimension   = 0;
static float dimMix      = 0.0f;
static float dimCooldown = 0.0f;
static float flash       = 0.0f;

// ---- camera
static float camYaw = 20.0f, camPitch = 22.0f, camDist = 13.0f;
static float camTx = 0, camTy = 1.2f, camTz = 0;
static float eyeX = 0, eyeY = 0, eyeZ = 0;
static int   lastMouseX = 0, lastMouseY = 0;
static bool  dragging = false;

static float timeSec = 0.0f;
static int   winW = 1000, winH = 650;

// ---------------------------------------------------------------- helpers
static float frand(float a, float b) {
    return a + (b - a) * (float)rand() / (float)RAND_MAX;
}
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float clamp1(float v) { return v > 1.0f ? 1.0f : (v < 0.0f ? 0.0f : v); }

static void fadeToward(float& f, bool on, float dt, float speed) {
    float t = on ? 1.0f : 0.0f;
    float s = speed * dt;
    if (f < t) { f += s; if (f > t) f = t; }
    else if (f > t) { f -= s; if (f < t) f = t; }
}

static float distToSeg(float x, float z, float x1, float z1, float x2, float z2) {
    float dx = x2 - x1, dz = z2 - z1;
    float l2 = dx * dx + dz * dz;
    float t = 0.0f;
    if (l2 > 0.0f) t = ((x - x1) * dx + (z - z1) * dz) / l2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float cx = x1 + dx * t - x, cz = z1 + dz * t - z;
    return sqrtf(cx * cx + cz * cz);
}

static float islandTop(const Island& is) {
    return is.y + sinf(timeSec * 0.8f + is.phase) * 0.25f;
}

static void showMsg(const char* s) {
    strncpy(msgText, s, sizeof(msgText) - 1);
    msgText[sizeof(msgText) - 1] = 0;
    msgTimer = 2.8f;
}

static void spawnBurst(float x, float y, float z, float r, float g, float b, int n) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.x = x; p.y = y; p.z = z;
        p.vx = frand(-3.0f, 3.0f);
        p.vy = frand(1.0f, 5.0f);
        p.vz = frand(-3.0f, 3.0f);
        p.life = frand(0.5f, 1.0f);
        p.r = r; p.g = g; p.b = b;
        particles.push_back(p);
    }
}

static void emitParticle(float x, float y, float z, float vx, float vy, float vz,
                         float life, float r, float g, float b) {
    Particle p = { x, y, z, vx, vy, vz, life, r, g, b };
    particles.push_back(p);
}

static void spawnDust(float x, float y, float z, int n) {
    for (int i = 0; i < n; ++i)
        emitParticle(x + frand(-0.3f, 0.3f), y + 0.05f, z + frand(-0.3f, 0.3f),
                     frand(-2.0f, 2.0f), frand(0.5f, 2.0f), frand(-2.0f, 2.0f),
                     frand(0.25f, 0.5f), 0.7f, 0.78f, 0.95f);
}

// ---------------------------------------------------------------- score & grade
static int gradeIndex(int s) {
    int g = 0;
    for (int i = 0; i < NUM_GRADES; ++i) if (s >= gradeNeed[i]) g = i;
    return g;
}

static void awardPoints(int pts) {
    if (pts <= 0) return;
    int before = gradeIndex(score);
    score += pts;
    int after = gradeIndex(score);
    if (after > before) {
        promoTimer = 3.5f;
        sprintf(promoText, "PROMOTED:  %s", gradeNames[after]);
        spawnBurst(px, py + 1.5f, pz, 1.0f, 0.85f, 0.3f, 50);
        camShake = 0.4f;
        if (after >= NUM_GRADES - 1 && graduateDelay < 0.0f) graduateDelay = 2.5f;   // graduation soon!
    }
}

// ---------------------------------------------------------------- builders
static void addIsland(float x, float y, float z, float sx, float sz,
                      float r, float g, float b) {
    Island i = { x, y, z, sx, sz, frand(0.0f, 6.28f), r, g, b };
    islands.push_back(i);
}

static void addBridge(float x, float y, float z, float sx, float sz, int dim) {
    Bridge b = { x, y, z, sx, sz, dim, (dim == 0) ? 1.0f : 0.0f };
    bridges.push_back(b);
}

static void addCrystal(float x, float y, float z, float r, float g, float b) {
    Crystal c = { x, y, z, r, g, b, frand(0.0f, 6.28f), false, false, 0.0f };
    crystals.push_back(c);
}

static void addLaser(float x1, float z1, float x2, float z2, float y, int dim,
                     float phase, float period, float onTime) {
    Laser l = { x1, z1, x2, z2, y, dim, phase, period, onTime,
                (dim <= 0) ? 1.0f : 0.0f, true, false };
    lasers.push_back(l);
}

static void addDoor(float x, float y, float z, float sx, float sz, float h, int dim, int needed) {
    Door d = { x, y, z, sx, sz, h, dim, 0.0f, (dim == 0) ? 1.0f : 0.0f, 0.0f, needed, 0 };
    doors.push_back(d);
}

static void addPlate(int island, float ox, float oz, int door, float hold) {
    Plate p = { island, ox, oz, door, hold, 0.0f, 0.0f, false };
    plates.push_back(p);
}

static void addCheckpoint(int island, float ox, float oz, bool active) {
    Checkpoint c = { island, ox, oz, active };
    checkpoints.push_back(c);
}

static void respawn() {
    const Checkpoint& c = checkpoints[curCP];
    const Island& is = islands[c.island];
    px = is.x + c.ox;
    pz = is.z + c.oz;
    py = islandTop(is) + 0.4f;
    vx = vy = vz = 0.0f;
    grounded = false;
    camTx = px; camTy = py + 1.2f; camTz = pz;
    recBuf.clear();
    if (dimension != 0) { dimension = 0; flash = 1.0f; }
}

static void damagePlayer(float amount) {
    health -= amount;
    invuln = 1.2f;
    damageFlash = 1.0f;
    camShake = 1.0f;
    vy = 7.0f;
    grounded = false;
    if (health <= 0.0f) {
        deaths++;
        health = MAX_HEALTH;
        respawn();
        showMsg("You were defeated - back to the last checkpoint");
    } else {
        showMsg("Laser hit!");
    }
}

static void addPortal(int island, float ox, float oz) {
    portal.island = island; portal.ox = ox; portal.oz = oz; portal.open = false;
    portalReady = true;
}

static void buildStars() {
    for (int i = 0; i < 400; ++i) {
        float a = frand(0.0f, 6.2832f);
        float b = frand(-1.0f, 1.0f);
        float r = 120.0f;
        float s = sqrtf(1.0f - b * b);
        Star st = { r * s * cosf(a), r * b, r * s * sinf(a) };
        stars.push_back(st);
    }
    for (int i = 0; i < NUM_MOTES; ++i) {
        motes[i][0] = frand(0.0f, 56.0f);
        motes[i][1] = frand(0.0f, 30.0f);
        motes[i][2] = frand(0.0f, 56.0f);
        motes[i][3] = frand(0.0f, 6.28f);
    }
}

// ---------------- LEVEL 1: Awakening
static void buildLevel1() {
    // islands
    addIsland(0.00f, 0.00f, 0.00f, 8.00f, 8.00f, 0.20f, 0.60f, 0.35f);   // 0
    addIsland(13.00f, 0.60f, 0.00f, 4.00f, 4.00f, 0.25f, 0.55f, 0.55f);   // 1
    addIsland(13.00f, 1.20f, -11.00f, 5.00f, 5.00f, 0.30f, 0.55f, 0.65f);   // 2
    addIsland(2.00f, 1.80f, -11.00f, 4.00f, 4.00f, 0.45f, 0.45f, 0.70f);   // 3
    addIsland(-8.00f, 2.40f, -11.00f, 5.00f, 5.00f, 0.55f, 0.40f, 0.70f);   // 4
    addIsland(-13.00f, 0.60f, 0.00f, 4.00f, 4.00f, 0.35f, 0.50f, 0.65f);   // 5
    addIsland(-13.00f, 1.20f, -9.00f, 4.00f, 4.00f, 0.30f, 0.45f, 0.75f);   // 6
    addIsland(-24.00f, 7.00f, -22.00f, 6.00f, 6.00f, 0.30f, 0.45f, 0.75f);   // 7
    addIsland(26.00f, 5.00f, 12.00f, 5.00f, 5.00f, 0.25f, 0.55f, 0.60f);   // 8
    addIsland(-2.00f, 9.00f, -30.00f, 6.00f, 5.00f, 0.45f, 0.40f, 0.70f);   // 9
    // bridges (0 = Reality, 1 = Echo)
    addBridge(7.50f, 0.30f, 0.00f, 7.60f, 2.00f, 0);
    addBridge(13.00f, 0.90f, -5.25f, 2.00f, 7.10f, 1);
    addBridge(7.50f, 1.50f, -11.00f, 7.10f, 2.00f, 0);
    addBridge(-3.00f, 2.10f, -11.00f, 6.10f, 2.00f, 1);
    addBridge(-7.50f, 0.30f, 0.00f, 7.60f, 2.00f, 1);
    addBridge(-13.00f, 0.90f, -4.50f, 2.00f, 5.60f, 0);
    // crystals
    addCrystal(3.00f, 1.70f, 3.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(-3.00f, 1.70f, -3.00f, 0.70f, 0.40f, 1.00f);
    addCrystal(3.00f, 1.70f, -3.00f, 0.40f, 0.70f, 1.00f);
    addCrystal(9.50f, 1.40f, 0.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(13.00f, 2.20f, 1.00f, 0.20f, 1.00f, 0.80f);
    addCrystal(13.00f, 2.00f, -7.50f, 0.70f, 0.40f, 1.00f);
    addCrystal(13.00f, 2.80f, -11.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(4.50f, 2.60f, -11.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(2.00f, 3.40f, -11.00f, 0.40f, 0.70f, 1.00f);
    addCrystal(-4.50f, 3.70f, -11.00f, 0.70f, 0.40f, 1.00f);
    addCrystal(-6.50f, 4.00f, -12.50f, 1.00f, 0.50f, 0.90f);
    addCrystal(-9.50f, 1.40f, 0.00f, 0.70f, 0.40f, 1.00f);
    addCrystal(-13.00f, 2.20f, 0.00f, 0.50f, 1.00f, 0.60f);
    addCrystal(-13.00f, 2.10f, -6.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(-13.00f, 2.80f, -9.00f, 1.00f, 0.50f, 0.90f);
    // lasers: x1, z1, x2, z2, y, dim, phase, period, on-time
    addLaser(6.00f, -1.40f, 6.00f, 1.40f, 1.10f, 0, 0.00f, 3.00f, 2.00f);
    addLaser(11.20f, -0.80f, 14.80f, -0.80f, 1.50f, -1, 1.00f, 2.60f, 1.60f);
    addLaser(12.00f, -5.50f, 14.00f, -5.50f, 1.70f, 1, 0.00f, 3.40f, 2.20f);
    addLaser(11.40f, -13.40f, 11.40f, -8.60f, 2.10f, -1, 0.50f, 3.00f, 1.80f);
    addLaser(6.50f, -12.00f, 6.50f, -10.00f, 2.30f, 0, 0.90f, 2.80f, 1.80f);
    addLaser(-8.50f, -1.40f, -8.50f, 1.40f, 1.10f, 1, 1.50f, 3.00f, 2.00f);
    // doors: x, y(base), z, sx, sz, height, dim, plates needed
    addDoor(-1.00f, 2.10f, -11.00f, 0.50f, 2.20f, 3.20f, 1, 1);
    addDoor(-13.00f, 0.90f, -3.30f, 2.20f, 0.50f, 3.20f, 0, 1);
    // pressure plates: island, ox, oz, door, hold seconds (0 = needs the Echo clone)
    addPlate(3, 0.90f, -0.90f, 0, 6.00f);
    addPlate(5, 1.00f, 1.00f, 1, 0.00f);
    // checkpoints (first one is the spawn point)
    addCheckpoint(0, 0.00f, 0.00f, true);
    addCheckpoint(1, -1.00f, 1.00f, false);
    addCheckpoint(2, 1.30f, 1.30f, false);
    addCheckpoint(3, -0.70f, 0.90f, false);
    addCheckpoint(4, 1.50f, 1.50f, false);
    addCheckpoint(5, -1.00f, 0.00f, false);
    addPortal(4, -1.20f, -1.20f);
}

// ---------------- LEVEL 2: Frozen Circuit
static void buildLevel2() {
    // islands
    addIsland(0.00f, 0.00f, 0.00f, 7.00f, 7.00f, 0.45f, 0.70f, 0.85f);   // 0
    addIsland(12.00f, 0.50f, 0.00f, 4.00f, 4.00f, 0.50f, 0.72f, 0.90f);   // 1
    addIsland(12.00f, 1.00f, -8.00f, 4.00f, 4.00f, 0.45f, 0.65f, 0.90f);   // 2
    addIsland(12.00f, 1.50f, -19.00f, 5.00f, 5.00f, 0.50f, 0.70f, 0.85f);   // 3
    addIsland(0.00f, 2.00f, -19.00f, 5.00f, 5.00f, 0.55f, 0.65f, 0.90f);   // 4
    addIsland(-12.00f, 2.50f, -19.00f, 5.00f, 5.00f, 0.50f, 0.60f, 0.90f);   // 5
    addIsland(-12.00f, 3.00f, -8.00f, 5.00f, 5.00f, 0.60f, 0.70f, 0.95f);   // 6
    addIsland(24.00f, 2.00f, -19.00f, 4.00f, 4.00f, 0.45f, 0.75f, 0.80f);   // 7
    addIsland(-24.00f, 8.00f, -6.00f, 6.00f, 6.00f, 0.40f, 0.60f, 0.90f);   // 8
    addIsland(30.00f, 6.00f, -6.00f, 5.00f, 5.00f, 0.45f, 0.65f, 0.85f);   // 9
    addIsland(0.00f, 10.00f, -34.00f, 6.00f, 5.00f, 0.50f, 0.60f, 0.90f);   // 10
    // bridges (0 = Reality, 1 = Echo)
    addBridge(6.75f, 0.25f, 0.00f, 7.10f, 2.00f, 1);
    addBridge(12.00f, 1.25f, -13.25f, 2.00f, 7.10f, 0);
    addBridge(6.00f, 1.75f, -19.00f, 7.60f, 2.00f, 1);
    addBridge(-6.00f, 2.25f, -19.00f, 7.60f, 2.00f, 0);
    addBridge(-12.00f, 2.75f, -13.50f, 2.00f, 6.60f, 1);
    addBridge(18.25f, 1.75f, -19.00f, 8.10f, 2.00f, 1);
    // crystals
    addCrystal(2.00f, 1.70f, 2.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(-2.00f, 1.70f, -2.00f, 0.70f, 0.40f, 1.00f);
    addCrystal(7.50f, 1.10f, 0.00f, 0.40f, 0.70f, 1.00f);
    addCrystal(12.00f, 2.10f, 1.00f, 0.20f, 1.00f, 0.80f);
    addCrystal(12.00f, 2.60f, -8.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(12.00f, 2.80f, -13.25f, 0.70f, 0.40f, 1.00f);
    addCrystal(12.00f, 3.10f, -19.00f, 0.40f, 0.70f, 1.00f);
    addCrystal(6.00f, 2.90f, -19.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(0.00f, 3.60f, -19.00f, 1.00f, 0.50f, 0.90f);
    addCrystal(-6.80f, 3.60f, -19.00f, 0.70f, 0.40f, 1.00f);
    addCrystal(-12.00f, 4.10f, -19.00f, 0.20f, 1.00f, 0.80f);
    addCrystal(-12.00f, 4.30f, -13.50f, 0.20f, 0.90f, 1.00f);
    addCrystal(-10.50f, 4.60f, -6.50f, 1.00f, 0.50f, 0.90f);
    addCrystal(18.25f, 2.90f, -19.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(23.00f, 3.60f, -18.00f, 0.70f, 0.40f, 1.00f);
    addCrystal(25.00f, 3.60f, -20.00f, 0.40f, 0.70f, 1.00f);
    addCrystal(24.00f, 3.90f, -19.00f, 1.00f, 0.50f, 0.90f);
    // lasers: x1, z1, x2, z2, y, dim, phase, period, on-time
    addLaser(5.50f, -1.40f, 5.50f, 1.40f, 1.05f, 1, 0.00f, 2.60f, 1.80f);
    addLaser(10.20f, -1.00f, 13.80f, -1.00f, 1.40f, -1, 0.80f, 2.40f, 1.40f);
    addLaser(10.20f, -8.00f, 13.80f, -8.00f, 1.90f, -1, 0.00f, 2.60f, 1.60f);
    addLaser(11.00f, -13.25f, 13.00f, -13.25f, 2.05f, 0, 0.00f, 3.00f, 2.00f);
    addLaser(11.20f, -21.30f, 11.20f, -16.70f, 2.40f, -1, 0.90f, 2.60f, 1.50f);
    addLaser(6.00f, -20.00f, 6.00f, -18.00f, 2.55f, 1, 0.40f, 2.80f, 1.80f);
    addLaser(-7.50f, -20.00f, -7.50f, -18.00f, 3.05f, 0, 0.60f, 3.00f, 1.90f);
    addLaser(-13.00f, -12.20f, -11.00f, -12.20f, 3.55f, 1, 0.00f, 2.80f, 1.80f);
    addLaser(17.00f, -20.00f, 17.00f, -18.00f, 2.55f, 1, 1.00f, 3.00f, 1.80f);
    // doors: x, y(base), z, sx, sz, height, dim, plates needed
    addDoor(-4.00f, 2.25f, -19.00f, 0.50f, 2.20f, 3.20f, 0, 1);
    addDoor(-12.00f, 2.75f, -14.80f, 2.20f, 0.50f, 3.20f, 1, 1);
    // pressure plates: island, ox, oz, door, hold seconds (0 = needs the Echo clone)
    addPlate(4, -1.20f, -1.20f, 0, 0.00f);
    addPlate(5, -1.20f, -1.20f, 1, 0.00f);
    // checkpoints (first one is the spawn point)
    addCheckpoint(0, 0.00f, 0.00f, true);
    addCheckpoint(1, -1.00f, 1.00f, false);
    addCheckpoint(3, 1.30f, 1.30f, false);
    addCheckpoint(4, 1.80f, 1.40f, false);
    addCheckpoint(5, 1.00f, 1.20f, false);
    addCheckpoint(6, 1.50f, 1.50f, false);
    addPortal(6, -1.20f, -1.20f);
}

// ---------------- LEVEL 3: The Echo Core
static void buildLevel3() {
    // islands
    addIsland(0.00f, 0.00f, 0.00f, 7.00f, 7.00f, 0.75f, 0.35f, 0.35f);   // 0
    addIsland(12.00f, 0.50f, 0.00f, 4.00f, 4.00f, 0.80f, 0.40f, 0.35f);   // 1
    addIsland(12.00f, 1.00f, -8.00f, 4.00f, 4.00f, 0.75f, 0.40f, 0.40f);   // 2
    addIsland(12.00f, 1.50f, -19.00f, 5.00f, 5.00f, 0.80f, 0.45f, 0.35f);   // 3
    addIsland(0.00f, 2.00f, -19.00f, 5.00f, 5.00f, 0.70f, 0.35f, 0.45f);   // 4
    addIsland(-12.00f, 2.50f, -19.00f, 5.00f, 5.00f, 0.75f, 0.35f, 0.50f);   // 5
    addIsland(-12.00f, 3.00f, -8.00f, 5.00f, 5.00f, 0.70f, 0.40f, 0.45f);   // 6
    addIsland(-24.00f, 3.50f, -8.00f, 5.00f, 5.00f, 0.80f, 0.35f, 0.55f);   // 7
    addIsland(24.00f, 1.30f, -8.00f, 4.00f, 4.00f, 0.75f, 0.45f, 0.35f);   // 8
    addIsland(-30.00f, 9.00f, -26.00f, 6.00f, 6.00f, 0.70f, 0.35f, 0.50f);   // 9
    addIsland(30.00f, 7.00f, -24.00f, 5.00f, 5.00f, 0.80f, 0.40f, 0.40f);   // 10
    addIsland(0.00f, 11.00f, -36.00f, 6.00f, 5.00f, 0.75f, 0.35f, 0.45f);   // 11
    // bridges (0 = Reality, 1 = Echo)
    addBridge(6.75f, 0.25f, 0.00f, 7.10f, 2.00f, 1);
    addBridge(12.00f, 1.25f, -13.25f, 2.00f, 7.10f, 0);
    addBridge(6.00f, 1.75f, -19.00f, 7.60f, 2.00f, 1);
    addBridge(-6.00f, 2.25f, -19.00f, 7.60f, 2.00f, 0);
    addBridge(-12.00f, 2.75f, -13.50f, 2.00f, 6.60f, 1);
    addBridge(-18.00f, 3.25f, -8.00f, 7.60f, 2.00f, 0);
    addBridge(18.00f, 1.15f, -8.00f, 8.60f, 2.00f, 1);
    // crystals
    addCrystal(2.50f, 1.70f, 2.50f, 0.20f, 0.90f, 1.00f);
    addCrystal(-2.50f, 1.70f, -2.50f, 0.70f, 0.40f, 1.00f);
    addCrystal(8.50f, 1.10f, 0.00f, 0.40f, 0.70f, 1.00f);
    addCrystal(12.00f, 2.10f, 1.00f, 0.20f, 1.00f, 0.80f);
    addCrystal(12.00f, 2.60f, -8.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(12.00f, 2.80f, -13.25f, 0.70f, 0.40f, 1.00f);
    addCrystal(12.00f, 3.10f, -19.00f, 0.40f, 0.70f, 1.00f);
    addCrystal(6.00f, 2.90f, -19.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(0.00f, 3.60f, -19.00f, 1.00f, 0.50f, 0.90f);
    addCrystal(-6.50f, 3.60f, -19.00f, 0.70f, 0.40f, 1.00f);
    addCrystal(-12.00f, 4.10f, -19.00f, 0.20f, 1.00f, 0.80f);
    addCrystal(-12.00f, 4.30f, -13.50f, 0.20f, 0.90f, 1.00f);
    addCrystal(-12.00f, 4.60f, -8.00f, 1.00f, 0.50f, 0.90f);
    addCrystal(-18.00f, 4.80f, -8.00f, 0.40f, 0.70f, 1.00f);
    addCrystal(-22.50f, 5.10f, -6.50f, 1.00f, 0.50f, 0.90f);
    addCrystal(18.00f, 2.30f, -8.00f, 0.20f, 0.90f, 1.00f);
    addCrystal(24.00f, 2.90f, -8.00f, 0.70f, 0.40f, 1.00f);
    addCrystal(23.00f, 2.90f, -7.00f, 0.40f, 0.70f, 1.00f);
    addCrystal(25.00f, 2.90f, -9.00f, 0.20f, 1.00f, 0.80f);
    addCrystal(24.00f, 3.20f, -8.50f, 1.00f, 0.50f, 0.90f);
    // lasers: x1, z1, x2, z2, y, dim, phase, period, on-time
    addLaser(5.50f, -1.40f, 5.50f, 1.40f, 1.05f, 1, 0.00f, 2.40f, 1.60f);
    addLaser(10.20f, -1.00f, 13.80f, -1.00f, 1.40f, -1, 0.50f, 2.20f, 1.20f);
    addLaser(10.20f, -8.00f, 13.80f, -8.00f, 1.90f, -1, 1.20f, 2.20f, 1.30f);
    addLaser(11.00f, -11.00f, 13.00f, -11.00f, 2.05f, 0, 0.30f, 2.60f, 1.60f);
    addLaser(11.00f, -15.60f, 13.00f, -15.60f, 2.05f, 0, 1.50f, 2.60f, 1.60f);
    addLaser(11.00f, -21.30f, 11.00f, -16.70f, 2.40f, -1, 0.90f, 2.40f, 1.40f);
    addLaser(4.50f, -20.00f, 4.50f, -18.00f, 2.55f, 1, 0.00f, 2.60f, 1.50f);
    addLaser(7.50f, -20.00f, 7.50f, -18.00f, 2.55f, 1, 1.30f, 2.60f, 1.50f);
    addLaser(0.50f, -21.20f, 0.50f, -16.80f, 2.90f, -1, 0.00f, 2.00f, 1.00f);
    addLaser(-7.50f, -20.00f, -7.50f, -18.00f, 3.05f, 0, 0.60f, 2.80f, 1.60f);
    addLaser(-14.30f, -17.00f, -9.70f, -17.00f, 3.40f, -1, 0.20f, 2.20f, 1.20f);
    addLaser(-13.00f, -14.50f, -11.00f, -14.50f, 3.55f, 1, 0.00f, 2.60f, 1.50f);
    addLaser(-13.00f, -12.20f, -11.00f, -12.20f, 3.55f, 1, 1.30f, 2.60f, 1.50f);
    addLaser(-14.30f, -10.20f, -9.70f, -10.20f, 3.90f, -1, 0.70f, 2.40f, 1.40f);
    addLaser(-20.50f, -9.00f, -20.50f, -7.00f, 4.05f, 0, 0.50f, 2.40f, 1.30f);
    addLaser(17.00f, -9.00f, 17.00f, -7.00f, 1.95f, 1, 0.90f, 2.60f, 1.60f);
    // doors: x, y(base), z, sx, sz, height, dim, plates needed
    addDoor(-4.00f, 2.25f, -19.00f, 0.50f, 2.20f, 3.20f, 0, 1);
    addDoor(-16.00f, 3.25f, -8.00f, 0.50f, 2.20f, 3.20f, 0, 2);
    // pressure plates: island, ox, oz, door, hold seconds (0 = needs the Echo clone)
    addPlate(4, -1.00f, -1.50f, 0, 0.00f);
    addPlate(6, -1.20f, -0.90f, 1, 0.00f);
    addPlate(6, -1.20f, 1.20f, 1, 7.00f);
    // checkpoints (first one is the spawn point)
    addCheckpoint(0, 0.00f, 0.00f, true);
    addCheckpoint(1, -1.00f, 1.00f, false);
    addCheckpoint(2, 0.00f, 1.40f, false);
    addCheckpoint(3, 1.30f, 1.30f, false);
    addCheckpoint(4, 1.80f, 1.40f, false);
    addCheckpoint(5, 1.00f, -1.50f, false);
    addCheckpoint(6, 1.40f, 1.40f, false);
    addCheckpoint(7, 1.50f, 1.50f, false);
    addPortal(7, -1.20f, -1.20f);
}

// ---- level table
static const char* levelNames[NUM_LEVELS] = {
    "Awakening",
    "Frozen Circuit",
    "The Echo Core"
};
static const char* levelHints[NUM_LEVELS] = {
    "Press E to switch dimensions: cyan bridges exist in Reality, purple in Echo.",
    "Stand on a pressure plate and press Q: your Echo clone will keep holding it for you.",
    "Two plates, one door: let your Echo hold one plate while you press the other."
};
static const int levelNeededTable[NUM_LEVELS] = { 10, 12, 14 };
// sky colors: Reality r,g,b then Echo r,g,b
static const float levelSky[NUM_LEVELS][6] = {
    { 0.03f, 0.05f, 0.18f, 0.15f, 0.03f, 0.24f },
    { 0.02f, 0.10f, 0.16f, 0.12f, 0.05f, 0.24f },
    { 0.12f, 0.04f, 0.07f, 0.20f, 0.02f, 0.22f }
};

// indices (in the order they are added in buildLevelN) of the crystals that are LOCKED.
// Level 1: 9 open + 6 locked, level 2: 10 open + 7 locked, level 3: 11 open + 9 locked.
// The portal needs more crystals than there are open ones, so every level
// forces you to solve a few puzzles.
static const int lockedCrystals[NUM_LEVELS][12] = {
    {  2,  4,  7,  9, 12, 14, -1, -1, -1, -1, -1, -1 },
    {  1,  3,  5,  8, 10, 12, 15, -1, -1, -1, -1, -1 },
    {  1,  3,  5,  8, 10, 12, 14, 17, 19, -1, -1, -1 }
};

static void clearLevel() {
    islands.clear(); bridges.clear(); crystals.clear(); lasers.clear();
    plates.clear(); doors.clear(); checkpoints.clear(); particles.clear();
    portalReady = false;
}

static void computeMapBounds() {
    bool first = true;
    for (size_t i = 0; i < islands.size(); ++i) {
        const Island& is = islands[i];
        if (is.y > 4.5f) continue;                 // far decorative islands
        float x0 = is.x - is.sx * 0.5f, x1 = is.x + is.sx * 0.5f;
        float z0 = is.z - is.sz * 0.5f, z1 = is.z + is.sz * 0.5f;
        if (first) { mapMinX = x0; mapMaxX = x1; mapMinZ = z0; mapMaxZ = z1; first = false; }
        if (x0 < mapMinX) mapMinX = x0;
        if (x1 > mapMaxX) mapMaxX = x1;
        if (z0 < mapMinZ) mapMinZ = z0;
        if (z1 > mapMaxZ) mapMaxZ = z1;
    }
    mapMinX -= 2.0f; mapMaxX += 2.0f; mapMinZ -= 2.0f; mapMaxZ += 2.0f;
    menuFx = (mapMinX + mapMaxX) * 0.5f;
    menuFz = (mapMinZ + mapMaxZ) * 0.5f;
    menuFy = 2.0f;
    float rx = (mapMaxX - mapMinX) * 0.5f, rz = (mapMaxZ - mapMinZ) * 0.5f;
    menuDist = sqrtf(rx * rx + rz * rz) * 1.5f + 10.0f;
}

static void loadLevel(int n) {
    if (n < 0) n = 0;
    if (n >= NUM_LEVELS) n = NUM_LEVELS - 1;
    curLevel = n;
    clearLevel();

    if (n == 0) buildLevel1();
    else if (n == 1) buildLevel2();
    else buildLevel3();

    for (int k = 0; k < 12 && lockedCrystals[n][k] >= 0; ++k)
        if (lockedCrystals[n][k] < (int)crystals.size()) crystals[lockedCrystals[n][k]].locked = true;

    levelNeeded = levelNeededTable[n];
    for (int i = 0; i < 3; ++i) { skyReal[i] = levelSky[n][i]; skyEcho[i] = levelSky[n][3 + i]; }

    levelStartScore = score; graduateDelay = -1.0f; promoTimer = 0.0f; puzzleStreak = 0;

    memset(keys, 0, sizeof(keys));
    health = MAX_HEALTH; invuln = 0.0f; damageFlash = 0.0f;
    crystalsGot = 0; falls = 0; deaths = 0;
    puzzlesSolved = 0; puzzlesWrong = 0; puzzleCooldown = 0.0f; puzzleCrystal = -1; nearCrystal = -1;
    levelTime = 0.0f;
    curCP = 0;
    dimension = 0; dimMix = 0.0f; dimCooldown = 0.0f; flash = 0.0f;
    ghostActive = false; echoCooldown = 0.0f;
    recBuf.clear();
    msgTimer = 0.0f;
    bannerTimer = 4.5f;
    gameState = STATE_PLAYING;
    fadeAlpha = 1.0f; camShake = 0.0f; ripple = -1.0f; squash = 0.0f; crystalPulse = 0.0f;
    computeMapBounds();
    respawn();
}

static void addLevelToTotals() {
    totalCrystals += crystalsGot;
    totalFalls    += falls;
    totalDefeats  += deaths;
    totalTime     += levelTime;
    totalPuzzles  += puzzlesSolved;
    totalWrong    += puzzlesWrong;
}

static void completeLevel() {
    addLevelToTotals();
    levelBonus = 30 + (deaths == 0 ? 20 : 0);
    awardPoints(levelBonus);
    graduateDelay = -1.0f;
    const Island& is = islands[portal.island];
    spawnBurst(is.x + portal.ox, islandTop(is) + 1.7f, is.z + portal.oz, 0.4f, 1.0f, 0.9f, 80);
    if (gradeIndex(score) >= NUM_GRADES - 1) gameState = STATE_GRADUATED;
    else gameState = (curLevel + 1 >= NUM_LEVELS) ? STATE_VICTORY : STATE_LEVEL_DONE;
}

static void graduateNow() {            // reached the top grade in the middle of a level
    addLevelToTotals();
    graduateDelay = -1.0f;
    gameState = STATE_GRADUATED;
}

// ---------------------------------------------------------------- dimension logic
static void switchDimension() {
    if (dimCooldown > 0.0f) return;
    dimension = 1 - dimension;
    dimCooldown = 0.35f;
    flash = 1.0f;
    ripple = 0.0f;
    camShake = 0.35f;

    // sparkles along every bridge, so you can see what appears / disappears
    for (size_t i = 0; i < bridges.size(); ++i) {
        const Bridge& b = bridges[i];
        float cr = (b.dim == 0) ? 0.10f : 0.85f;
        float cg = (b.dim == 0) ? 0.85f : 0.25f;
        int n = (int)(b.sx * b.sz * 1.2f) + 6;
        if (n > 30) n = 30;
        for (int k = 0; k < n; ++k)
            emitParticle(b.x + frand(-b.sx * 0.5f, b.sx * 0.5f), b.y, b.z + frand(-b.sz * 0.5f, b.sz * 0.5f),
                         frand(-0.5f, 0.5f), frand(1.5f, 4.0f), frand(-0.5f, 0.5f),
                         frand(0.5f, 1.0f), cr, cg, 1.0f);
    }
}

static void updateDimension(float dt) {
    float target = (float)dimension;
    float step = 2.5f * dt;
    if (dimMix < target) { dimMix += step; if (dimMix > target) dimMix = target; }
    if (dimMix > target) { dimMix -= step; if (dimMix < target) dimMix = target; }

    for (size_t i = 0; i < bridges.size(); ++i)
        fadeToward(bridges[i].fade, bridges[i].dim == dimension, dt, 3.5f);
    for (size_t i = 0; i < doors.size(); ++i)
        fadeToward(doors[i].fade, doors[i].dim == dimension, dt, 3.5f);
    for (size_t i = 0; i < lasers.size(); ++i)
        fadeToward(lasers[i].fade, lasers[i].dim < 0 || lasers[i].dim == dimension, dt, 4.0f);

    if (dimCooldown > 0.0f) dimCooldown -= dt;
    if (flash > 0.0f) { flash -= 1.8f * dt; if (flash < 0.0f) flash = 0.0f; }
}

static void buildSolids() {
    solids.clear();
    for (size_t i = 0; i < islands.size(); ++i) {
        const Island& is = islands[i];
        Solid s = { is.x, is.z, is.sx, is.sz, islandTop(is), 1.0f };
        solids.push_back(s);
    }
    for (size_t i = 0; i < bridges.size(); ++i) {
        const Bridge& b = bridges[i];
        if (b.fade > 0.5f) {
            Solid s = { b.x, b.z, b.sx, b.sz, b.y, 0.4f };
            solids.push_back(s);
        }
    }
}

// ---------------------------------------------------------------- player logic
static void updatePlayer(float dt) {
    buildSolids();

    float yaw = camYaw * (float)M_PI / 180.0f;
    float fx = -sinf(yaw), fz = -cosf(yaw);
    float rx =  cosf(yaw), rz = -sinf(yaw);

    float fwd = (keys['w'] ? 1.0f : 0.0f) - (keys['s'] ? 1.0f : 0.0f);
    float rgt = (keys['d'] ? 1.0f : 0.0f) - (keys['a'] ? 1.0f : 0.0f);
    float dx = fx * fwd + rx * rgt;
    float dz = fz * fwd + rz * rgt;
    float len = sqrtf(dx * dx + dz * dz);

    float tvx = 0.0f, tvz = 0.0f;
    if (len > 0.001f) {
        dx /= len; dz /= len;
        tvx = dx * MOVE_SPEED;
        tvz = dz * MOVE_SPEED;

        float target = atan2f(dx, dz) * 180.0f / (float)M_PI;
        float d = target - heading;
        while (d >  180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        float k = 12.0f * dt; if (k > 1.0f) k = 1.0f;
        heading += d * k;
    }

    float k = (grounded ? 14.0f : 6.0f) * dt;
    if (k > 1.0f) k = 1.0f;
    vx += (tvx - vx) * k;
    vz += (tvz - vz) * k;

    px += vx * dt;
    pz += vz * dt;

    // --- side collision with islands / bridges
    for (size_t i = 0; i < solids.size(); ++i) {
        const Solid& s = solids[i];
        float bottom = s.top - s.thick;
        if (py + PLAYER_H > bottom && py < s.top - 0.8f) {
            float hx = s.sx * 0.5f + PLAYER_R;
            float hz = s.sz * 0.5f + PLAYER_R;
            float ddx = px - s.x, ddz = pz - s.z;
            if (fabsf(ddx) < hx && fabsf(ddz) < hz) {
                float penX = hx - fabsf(ddx);
                float penZ = hz - fabsf(ddz);
                if (penX < penZ) px += (ddx >= 0.0f ? penX : -penX);
                else             pz += (ddz >= 0.0f ? penZ : -penZ);
            }
        }
    }

    // --- closed doors block the way
    for (size_t i = 0; i < doors.size(); ++i) {
        const Door& d = doors[i];
        if (d.fade <= 0.5f || d.open >= 0.85f) continue;
        float lift = d.open * (d.h + 0.1f);
        float y0 = d.y + lift, y1 = y0 + d.h;
        if (py < y1 && py + PLAYER_H > y0) {
            float hx = d.sx * 0.5f + PLAYER_R;
            float hz = d.sz * 0.5f + PLAYER_R;
            float ddx = px - d.x, ddz = pz - d.z;
            if (fabsf(ddx) < hx && fabsf(ddz) < hz) {
                float penX = hx - fabsf(ddx);
                float penZ = hz - fabsf(ddz);
                if (penX < penZ) { px += (ddx >= 0.0f ? penX : -penX); vx = 0.0f; }
                else             { pz += (ddz >= 0.0f ? penZ : -penZ); vz = 0.0f; }
            }
        }
    }

    // --- jump
    if (grounded) coyote = 0.12f; else coyote -= dt;
    if (keys[' '] && coyote > 0.0f) {
        vy = JUMP_V;
        coyote = 0.0f;
        grounded = false;
        squash = -0.28f;                 // stretch
        spawnDust(px, py, pz, 8);
    }

    // --- gravity
    vy -= GRAVITY * dt;
    if (vy < -28.0f) vy = -28.0f;
    py += vy * dt;

    // --- landing + head bump
    bool  wasGrounded = grounded;
    float vyBefore = vy;
    grounded = false;
    for (size_t i = 0; i < solids.size(); ++i) {
        const Solid& s = solids[i];
        float bottom = s.top - s.thick;
        float m = 0.15f;
        bool inside = fabsf(px - s.x) <= s.sx * 0.5f + m &&
                      fabsf(pz - s.z) <= s.sz * 0.5f + m;
        if (!inside) continue;

        if (vy > 0.0f && py + PLAYER_H > bottom && py < bottom) {
            py = bottom - PLAYER_H;
            vy = 0.0f;
        }
        if (vy <= 0.0f && py <= s.top + 0.05f && py >= s.top - 0.8f) {
            py = s.top;
            vy = 0.0f;
            grounded = true;
        }
    }

    float hs = sqrtf(vx * vx + vz * vz);
    if (grounded) walkPhase += hs * dt * 1.3f;

    // landing squash + dust, footstep dust
    if (grounded && !wasGrounded && vyBefore < -7.0f) {
        float s = -vyBefore * 0.02f;
        squash = s > 0.4f ? 0.4f : s;
        spawnDust(px, py, pz, 10);
        if (vyBefore < -16.0f) camShake = 0.3f;
    }
    if (grounded && hs > 3.0f) {
        stepTimer -= dt;
        if (stepTimer <= 0.0f) { stepTimer = 0.13f; spawnDust(px, py, pz, 2); }
    }

    if (py < FALL_LIMIT) {
        falls++;
        respawn();
        showMsg("You fell - back to the last checkpoint");
    }
}

// ---------------------------------------------------------------- world logic
// ---------------------------------------------------------------- crystal puzzle lock
static int irand(int a, int b) {              // random integer in [a, b]
    return a + rand() % (b - a + 1);
}

static void collectCrystal(int i) {
    Crystal& c = crystals[i];
    if (c.collected) return;
    float bob = sinf(timeSec * 1.6f + c.phase) * 0.25f;
    c.collected = true;
    crystalsGot++;
    if (!c.locked) awardPoints(5);
    crystalPulse = 1.0f;
    spawnBurst(c.x, c.y + bob, c.z, c.r, c.g, c.b, 30);
    char buf[64];
    sprintf(buf, "%s  %d / %d", c.locked ? "Crystal unlocked" : "Crystal collected", crystalsGot, (int)crystals.size());
    showMsg(buf);
}

static int gcdi(int a, int b) { return b == 0 ? a : gcdi(b, a % b); }

static void toBin(int v, int bits, char* out) {
    for (int i = 0; i < bits; ++i) out[i] = ((v >> (bits - 1 - i)) & 1) ? '1' : '0';
    out[bits] = 0;
}
static int binLen(int v) { int n = 0; while ((v >> n) > 0) n++; return n ? n : 1; }

static int gateEval(int t, int A, int B, int C) {
    switch (t) {
        case 0:  return (A && B) || C;
        case 1:  return (A ^ B) && C;
        case 2:  return !(A && B) || C;
        default: return (A || B) && !C;
    }
}
static const char* gateText[4] = { "(A AND B) OR C", "(A XOR B) AND C", "(A NAND B) OR C", "(A OR B) AND (NOT C)" };

// ---------------- MATH (typed answer)
static void genMath(int d) {
    if (d == 0) {
        int k = irand(0, 3);
        if (k == 0)      { int a = irand(8, 40), b = irand(5, 40); puzzleAnswer = a + b; sprintf(puzzleQuestion, "%d + %d = ?", a, b); }
        else if (k == 1) { int a = irand(20, 70), b = irand(5, a - 5); puzzleAnswer = a - b; sprintf(puzzleQuestion, "%d - %d = ?", a, b); }
        else if (k == 2) { int s = irand(1, 20), dd = irand(2, 9); puzzleAnswer = s + 4 * dd;
                           sprintf(puzzleQuestion, "%d, %d, %d, %d, ?", s, s + dd, s + 2 * dd, s + 3 * dd); }
        else             { int n = irand(2, 9); puzzleAnswer = n * n; sprintf(puzzleQuestion, "%d squared = ?", n); }
    } else if (d == 1) {
        int k = irand(0, 3);
        if (k == 0)      { int a = irand(3, 12), b = irand(3, 12); puzzleAnswer = a * b; sprintf(puzzleQuestion, "%d x %d = ?", a, b); }
        else if (k == 1) {
            if (irand(0, 1) == 0) { int x = irand(3, 25), a = irand(5, 30); puzzleAnswer = x; sprintf(puzzleQuestion, "x + %d = %d", a, x + a); }
            else                  { int x = irand(15, 40), a = irand(3, 12); puzzleAnswer = x; sprintf(puzzleQuestion, "x - %d = %d", a, x - a); }
        }
        else if (k == 2) { int s = irand(1, 4), m = irand(2, 3); puzzleAnswer = s * m * m * m * m;
                           sprintf(puzzleQuestion, "%d, %d, %d, %d, ?", s, s * m, s * m * m, s * m * m * m); }
        else             { int n = irand(4, 15); puzzleAnswer = n * n; sprintf(puzzleQuestion, "%d squared = ?", n); }
    } else {
        int k = irand(0, 4);
        if (k == 0) {
            int a = irand(2, 15), b = irand(2, 9), c = irand(2, 9);
            if (irand(0, 1) == 0) { puzzleAnswer = (a + b) * c; sprintf(puzzleQuestion, "(%d + %d) x %d = ?", a, b, c); }
            else                  { puzzleAnswer = a + b * c;   sprintf(puzzleQuestion, "%d + %d x %d = ?", a, b, c); }
        } else if (k == 1) { int a = irand(3, 9), x = irand(3, 15); puzzleAnswer = x; sprintf(puzzleQuestion, "%d x ? = %d", a, a * x); }
        else if (k == 2) { static const int ps[4] = { 10, 20, 25, 50 }; int p = ps[irand(0, 3)], n = 20 * irand(2, 15);
                           puzzleAnswer = n * p / 100; sprintf(puzzleQuestion, "%d%% of %d = ?", p, n); }
        else if (k == 3) { int g = irand(2, 9), m = irand(2, 7), n = irand(2, 7); if (m == n) n++;
                           int a = g * m, b = g * n; puzzleAnswer = gcdi(a, b); sprintf(puzzleQuestion, "GCD of %d and %d = ?", a, b); }
        else             { int n = irand(10, 25); puzzleAnswer = n * (n + 1) / 2; sprintf(puzzleQuestion, "1 + 2 + 3 + ... + %d = ?", n); }
    }
}

// ---------------- LOGIC (typed answer)
static void genLogic(int d) {
    if (d == 0) {
        int k = irand(0, 2);
        if (k == 0)      { int a = irand(1, 3), b = irand(1, 4); puzzleAnswer = 2 * a + 3 * b;
                           sprintf(puzzleQuestion, "%d, %d, %d, %d, ?", a, b, a + b, a + 2 * b); }
        else if (k == 1) { int n = irand(10, 20), g = irand(2, 8), b = irand(2, 9); puzzleAnswer = n - g + b;
                           sprintf(puzzleQuestion, "%d apples, give away %d, buy %d more: total?", n, g, b); }
        else             { int c = irand(2, 6), n1 = irand(2, 4), n2 = n1 + irand(1, 4); puzzleAnswer = c * n2;
                           sprintf(puzzleQuestion, "%d pens cost %d. %d pens cost ?", n1, c * n1, n2); }
    } else if (d == 1) {
        int k = irand(0, 3);
        if (k == 0)      { int c = irand(3, 8); puzzleAnswer = c + 1;
                           sprintf(puzzleQuestion, "Socks in %d colors, in the dark. Min socks to surely get a pair?", c); }
        else if (k == 1) { int n = irand(4, 8); puzzleAnswer = n * (n - 1) / 2;
                           sprintf(puzzleQuestion, "%d people, everyone shakes hands once. Total?", n); }
        else if (k == 2) { int s = irand(1, 9); puzzleAnswer = s + 15;
                           sprintf(puzzleQuestion, "%d, %d, %d, %d, %d, ?", s, s + 1, s + 3, s + 6, s + 10); }
        else             { int v = irand(30, 90), t = irand(2, 5); puzzleAnswer = v * t;
                           sprintf(puzzleQuestion, "A train goes %d km/h for %d h. Distance (km)?", v, t); }
    } else {
        int k = irand(0, 2);
        if (k == 0)      { int n = irand(9, 15); puzzleAnswer = n * (n - 1) / 2;
                           sprintf(puzzleQuestion, "%d people, everyone shakes hands once. Total?", n); }
        else if (k == 1) { int x = irand(1, 9), dd = irand(2, 5), y = irand(10, 30), e = irand(5, 12); puzzleAnswer = x + 3 * dd;
                           sprintf(puzzleQuestion, "%d, %d, %d, %d, %d, %d, ?", x, y, x + dd, y + e, x + 2 * dd, y + 2 * e); }
        else {
            int c = irand(4, 7), s = irand(1, c - 2), h = irand(12, 30), pos = 0, day = 0;
            while (true) { day++; pos += c; if (pos >= h) break; pos -= s; }
            puzzleAnswer = day;
            sprintf(puzzleQuestion, "Snail: +%d m by day, -%d m by night. Well is %d m deep. Days to get out?", c, s, h);
        }
    }
}

// ---------------- TECH (typed answer)
static void genTech(int d) {
    char bin[16];
    if (d == 0) {
        int k = irand(0, 3);
        if (k == 0)      { int v = irand(3, 15); toBin(v, 4, bin); puzzleAnswer = v; sprintf(puzzleQuestion, "%s (binary) = ? (decimal)", bin); }
        else if (k == 1) { int n = irand(2, 9); puzzleAnswer = n * 8; sprintf(puzzleQuestion, "%d bytes = ? bits", n); }
        else if (k == 2) { int n = irand(3, 12); puzzleAnswer = n; sprintf(puzzleQuestion, "for (i=0; i<%d; i++)  How many times does the loop run?", n); }
        else             { int a = irand(1, 9), b = irand(1, 9), c = irand(2, 4); puzzleAnswer = (a + b) * c;
                           sprintf(puzzleQuestion, "x = %d;  x = x + %d;  x = x * %d;   x = ?", a, b, c); }
    } else if (d == 1) {
        int k = irand(0, 4);
        if (k == 0)      { int v = irand(5, 40); int n = binLen(v); toBin(v, n, bin); puzzleAnswer = atoi(bin);
                           sprintf(puzzleQuestion, "%d (decimal) = ? (binary)", v); }
        else if (k == 1) { int v = irand(16, 63); toBin(v, 6, bin); puzzleAnswer = v; sprintf(puzzleQuestion, "%s (binary) = ? (decimal)", bin); }
        else if (k == 2) { int n = irand(3, 9); puzzleAnswer = 1 << n; sprintf(puzzleQuestion, "1 << %d = ?", n); }
        else if (k == 3) { int a = irand(10, 60), b = irand(3, 9); puzzleAnswer = a % b; sprintf(puzzleQuestion, "%d %% %d = ?", a, b); }
        else             { int n = irand(4, 10); puzzleAnswer = n * (n + 1) / 2;
                           sprintf(puzzleQuestion, "s=0; for(i=1; i<=%d; i++) s+=i;   s = ?", n); }
    } else {
        int k = irand(0, 4);
        if (k == 0)      { int v = irand(16, 255); puzzleAnswer = v; sprintf(puzzleQuestion, "0x%X = ? (decimal)", v); }
        else if (k == 1) { int a = irand(5, 15), b = irand(5, 15), op = irand(0, 2);
                           puzzleAnswer = (op == 0) ? (a & b) : (op == 1) ? (a | b) : (a ^ b);
                           sprintf(puzzleQuestion, "%d %s %d = ?  (bitwise, decimal)", a, op == 0 ? "AND" : (op == 1 ? "OR" : "XOR"), b); }
        else if (k == 2) { int n = irand(24, 30); puzzleAnswer = 1 << (32 - n); sprintf(puzzleQuestion, "A /%d subnet has how many IP addresses (total)?", n); }
        else if (k == 3) { int a = irand(0, 5), s = irand(2, 5), cnt = irand(4, 9), b = a + s * cnt - irand(0, s - 1);
                           puzzleAnswer = (b - a + s - 1) / s;
                           sprintf(puzzleQuestion, "for (i=%d; i<%d; i+=%d)  How many iterations?", a, b, s); }
        else             { int t = irand(0, 3), cnt = 0;
                           for (int m = 0; m < 8; ++m) cnt += gateEval(t, m & 1, (m >> 1) & 1, (m >> 2) & 1);
                           puzzleAnswer = cnt; sprintf(puzzleQuestion, "Inputs A,B,C giving 1 (of 8):  %s", gateText[t]); }
    }
}

// ---------------- MULTIPLE CHOICE table (first option = correct, shuffled at runtime)
struct Quiz { const char* q; const char* o[4]; int cat; int diff; };
static const Quiz quiz[] = {
    // TECH easy
    { "What does CPU stand for?", { "Central Processing Unit", "Computer Power Unit", "Central Program Utility", "Core Pixel Unit" }, 2, 0 },
    { "Which of these is an operating system?", { "Linux", "Python", "HTML", "Excel" }, 2, 0 },
    { "Which memory loses its data when power is off?", { "RAM", "SSD", "Hard disk", "ROM" }, 2, 0 },
    { "Which one is an INPUT device?", { "Keyboard", "Monitor", "Speaker", "Printer" }, 2, 0 },
    { "1 byte equals how many bits?", { "8", "4", "16", "10" }, 2, 0 },
    // TECH medium
    { "Which data structure works as LIFO?", { "Stack", "Queue", "Array", "Graph" }, 2, 1 },
    { "Which data structure works as FIFO?", { "Queue", "Stack", "Tree", "Set" }, 2, 1 },
    { "Which protocol is used for secure web browsing?", { "HTTPS", "FTP", "SMTP", "DHCP" }, 2, 1 },
    { "What does DNS do?", { "Turns domain names into IP addresses", "Encrypts your files", "Compresses images", "Cools the CPU" }, 2, 1 },
    { "Which of these is a sorting algorithm?", { "Merge sort", "Binary search", "Hash table", "Linked list" }, 2, 1 },
    { "Which gate gives 1 only when both inputs are 1?", { "AND", "OR", "XOR", "NOT" }, 2, 1 },
    // TECH hard
    { "Worst-case time of binary search on n items?", { "O(log n)", "O(n)", "O(n^2)", "O(1)" }, 2, 2 },
    { "Worst-case time of bubble sort?", { "O(n^2)", "O(n log n)", "O(log n)", "O(n)" }, 2, 2 },
    { "Which OSI layer is responsible for routing packets?", { "Network layer", "Physical layer", "Session layer", "Application layer" }, 2, 2 },
    { "Which gate is called a universal gate?", { "NAND", "XOR", "AND", "XNOR" }, 2, 2 },
    { "What is the default port of HTTPS?", { "443", "80", "22", "21" }, 2, 2 },
    { "Which tree traversal visits a BST in sorted order?", { "In-order", "Pre-order", "Post-order", "Level-order" }, 2, 2 },
    { "What does git commit do?", { "Saves a snapshot of staged changes", "Uploads code to the server", "Downloads a repository", "Deletes a branch" }, 2, 2 },
    // LOGIC easy
    { "Which number comes next: 2, 4, 6, 8, ?", { "10", "12", "9", "14" }, 1, 0 },
    { "Which one does not belong: Cat, Dog, Lion, Car?", { "Car", "Cat", "Dog", "Lion" }, 1, 0 },
    { "Tom is taller than Sam. Sam is taller than Raj. Shortest?", { "Raj", "Sam", "Tom", "Cannot tell" }, 1, 0 },
    // LOGIC medium
    { "All robots are machines. Which statement must be true?", { "Every robot is a machine", "Every machine is a robot", "No machine is a robot", "Every robot is fast" }, 1, 1 },
    { "Which number is NOT prime: 11, 13, 15, 17?", { "15", "11", "13", "17" }, 1, 1 },
    { "Today is Monday. What day is it after 10 days?", { "Thursday", "Wednesday", "Friday", "Sunday" }, 1, 1 },
    // LOGIC hard
    { "Every Echo is a Shift. No Shift is a Crystal. So:", { "No Echo is a Crystal", "Some Echo is a Crystal", "Every Crystal is an Echo", "Every Shift is an Echo" }, 1, 2 },
    { "Angle between the clock hands at 3:15?", { "7.5 degrees", "0 degrees", "15 degrees", "30 degrees" }, 1, 2 },
    { "3 switches outside, 1 bulb inside. Min room visits to find the switch?", { "1", "2", "3", "0" }, 1, 2 },
    { "Bat + ball = 110. Bat costs 100 more than ball. Ball = ?", { "5", "10", "1", "15" }, 1, 2 }
};
static const int NUM_QUIZ = (int)(sizeof(quiz) / sizeof(quiz[0]));
static bool quizUsed[64];

static bool pickChoice(int cat, int diff) {
    int idx[64], n = 0;
    for (int i = 0; i < NUM_QUIZ; ++i)
        if (quiz[i].cat == cat && quiz[i].diff == diff && !quizUsed[i]) idx[n++] = i;
    if (n == 0) {                                   // all used: start over for this group
        for (int i = 0; i < NUM_QUIZ; ++i)
            if (quiz[i].cat == cat && quiz[i].diff == diff) { quizUsed[i] = false; idx[n++] = i; }
        if (n == 0) return false;
    }
    int q = idx[rand() % n];
    quizUsed[q] = true;
    int order[4] = { 0, 1, 2, 3 };
    for (int i = 3; i > 0; --i) { int j = rand() % (i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
    strncpy(puzzleQuestion, quiz[q].q, sizeof(puzzleQuestion) - 1);
    puzzleQuestion[sizeof(puzzleQuestion) - 1] = 0;
    for (int i = 0; i < 4; ++i) {
        strncpy(puzzleOpt[i], quiz[q].o[order[i]], 43);
        puzzleOpt[i][43] = 0;
        if (order[i] == 0) puzzleCorrect = i;
    }
    return true;
}

static int pickDifficulty() {
    int r = irand(0, 99);
    if (curLevel == 0) return (r < 65) ? 0 : (r < 95 ? 1 : 2);
    if (curLevel == 1) return (r < 20) ? 0 : (r < 75 ? 1 : 2);
    return (r < 5) ? 0 : (r < 40 ? 1 : 2);
}

static void generatePuzzle() {
    puzzleDiff = pickDifficulty();
    int r = irand(0, 99);
    puzzleCat = (r < 40) ? 0 : (r < 65 ? 1 : 2);
    puzzleMC = false;
    for (int i = 0; i < 4; ++i) puzzleOptOff[i] = false;
    if (puzzleCat != 0 && irand(0, 99) < 45 && pickChoice(puzzleCat, puzzleDiff)) puzzleMC = true;
    else if (puzzleCat == 0) genMath(puzzleDiff);
    else if (puzzleCat == 1) genLogic(puzzleDiff);
    else                     genTech(puzzleDiff);
    puzzleHint[0] = 0;
}

static void startPuzzle(int crystalIndex) {
    puzzleCrystal = crystalIndex;
    generatePuzzle();
    puzzleInput[0] = 0;
    puzzleLen = 0;
    puzzleAttempts = 0;
    puzzleResult = 0;
    puzzleTimer = 0.0f;
    puzzleEarned = 0;
    vx = vz = 0.0f;
    gameState = STATE_PUZZLE;
    showMsg("This crystal is locked - solve the puzzle!");
}

static void closePuzzle(bool solved, float cooldown) {
    int idx = puzzleCrystal;
    puzzleCooldown = 0.0f;
    puzzleCrystal = -1;
    invuln = (invuln > 1.0f) ? invuln : 1.0f;          // short grace period after returning
    gameState = STATE_PLAYING;
    if (solved && idx >= 0) {
        collectCrystal(idx);
        awardPoints(puzzleEarned);
        char b[64];
        sprintf(b, "Crystal unlocked!  +%d points", puzzleEarned);
        showMsg(b);
    }
    if (!solved) {
        puzzleStreak = 0;
        if (idx >= 0) crystals[idx].lockTimer = cooldown;
    }
}

static void judgePuzzle(bool correct) {
    if (correct) {
        float f = (puzzleAttempts == 0) ? 1.0f : (puzzleAttempts == 1 ? 0.7f : 0.4f);
        puzzleEarned = (int)(diffBase[puzzleDiff] * f + 0.5f);
        if (puzzleAttempts == 0) puzzleEarned += 3 * (puzzleStreak > 3 ? 3 : puzzleStreak);   // streak bonus
        puzzleStreak = (puzzleAttempts == 0) ? puzzleStreak + 1 : 0;
        puzzleResult = 1;
        puzzleTimer = 1.3f;
        puzzlesSolved++;
        const Crystal& c = crystals[puzzleCrystal];
        spawnBurst(c.x, c.y, c.z, 0.4f, 1.0f, 0.5f, 40);
        return;
    }
    puzzleAttempts++;
    puzzlesWrong++;
    puzzleStreak = 0;
    camShake = 0.6f;
    puzzleInput[0] = 0;
    puzzleLen = 0;
    if (puzzleAttempts >= PUZZLE_MAX_TRIES) { puzzleResult = 2; puzzleTimer = 1.3f; return; }
    if (puzzleMC) { sprintf(puzzleHint, "Not that one - choose another option"); return; }
    if (puzzleAttempts == 1) {
        int width = puzzleAnswer / 4;
        if (width < 4) width = 4;
        int lo = puzzleAnswer - irand(1, width - 1);
        if (lo < 0) lo = 0;
        sprintf(puzzleHint, "Hint: the answer is between %d and %d", lo, lo + width);
    } else {
        sprintf(puzzleHint, "Hint: the last digit is %d", ((puzzleAnswer % 10) + 10) % 10);
    }
}

static void submitPuzzle() {
    if (puzzleLen == 0 || (puzzleLen == 1 && puzzleInput[0] == '-')) return;
    judgePuzzle(atoi(puzzleInput) == puzzleAnswer);
}

static void puzzleKey(unsigned char key) {
    if (puzzleResult != 0) return;                       // result animation running
    if (puzzleMC) {                                      // choose with keys 1-4
        if (key >= '1' && key <= '4') {
            int i = key - '1';
            if (puzzleOptOff[i]) return;
            if (i == puzzleCorrect) judgePuzzle(true);
            else { puzzleOptOff[i] = true; judgePuzzle(false); }
        }
        return;
    }
    if (key >= '0' && key <= '9') {
        if (puzzleLen < 6) { puzzleInput[puzzleLen++] = (char)key; puzzleInput[puzzleLen] = 0; }
    } else if (key == '-') {
        if (puzzleLen == 0) { puzzleInput[puzzleLen++] = '-'; puzzleInput[puzzleLen] = 0; }
    } else if (key == 8 || key == 127) {
        if (puzzleLen > 0) puzzleInput[--puzzleLen] = 0;
    } else if (key == 13) {
        submitPuzzle();
    }
}

static void tryOpenPuzzle() {
    if (nearCrystal < 0 || nearCrystal >= (int)crystals.size() || crystals[nearCrystal].collected) {
        showMsg("No locked crystal nearby - stand next to a caged crystal and press F");
        return;
    }
    const Crystal& c = crystals[nearCrystal];
    if (c.lockTimer > 0.0f) {
        char buf[64];
        sprintf(buf, "This crystal is locked out for %.0f more seconds", c.lockTimer + 0.5f);
        showMsg(buf);
        return;
    }
    startPuzzle(nearCrystal);
}

static void updatePuzzle(float dt) {
    if (puzzleResult == 0) return;
    puzzleTimer -= dt;
    if (puzzleTimer <= 0.0f) closePuzzle(puzzleResult == 1, puzzleResult == 1 ? 0.5f : 5.0f);
}

static void updateParticles(float dt) {
    for (size_t i = 0; i < particles.size(); ) {
        Particle& p = particles[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            particles[i] = particles.back();
            particles.pop_back();
            continue;
        }
        p.vy -= 6.0f * dt;
        p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
        ++i;
    }
}

static void updateWorld(float dt) {
    if (invuln > 0.0f)      invuln -= dt;
    if (damageFlash > 0.0f) { damageFlash -= 2.0f * dt; if (damageFlash < 0.0f) damageFlash = 0.0f; }
    if (msgTimer > 0.0f)    msgTimer -= dt;

    // ---- crystals
    nearCrystal = -1;
    float nearD2 = 1.0e9f;
    for (size_t i = 0; i < crystals.size(); ++i) {
        Crystal& c = crystals[i];
        if (c.lockTimer > 0.0f) c.lockTimer -= dt;
        if (c.collected) continue;
        float bob = sinf(timeSec * 1.6f + c.phase) * 0.25f;
        float ddx = px - c.x, ddy = (py + 1.0f) - (c.y + bob), ddz = pz - c.z;
        float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
        if (!c.locked) {
            if (d2 < 1.4f * 1.4f) collectCrystal((int)i);          // normal crystal: just touch it
        } else if (d2 < 2.4f * 2.4f && d2 < nearD2) {
            nearCrystal = (int)i;                                  // locked crystal: press F
            nearD2 = d2;
        }
    }

    // ---- lasers
    for (size_t i = 0; i < lasers.size(); ++i) {
        Laser& l = lasers[i];
        if (l.period <= 0.0f || l.onTime >= l.period) {
            l.on = true; l.warn = false;
        } else {
            float t = fmodf(timeSec + l.phase, l.period);
            l.on = t < l.onTime;
            l.warn = (!l.on) && (l.period - t < 0.5f);
        }
        if (l.fade > 0.5f && l.on && invuln <= 0.0f) {
            if (py <= l.y && py + PLAYER_H >= l.y &&
                distToSeg(px, pz, l.x1, l.z1, l.x2, l.z2) < PLAYER_R + 0.1f) {
                damagePlayer(LASER_DMG);
                break;
            }
        }
    }

    // ---- pressure plates -> doors (the player OR the Echo clone can press a plate)
    for (size_t i = 0; i < doors.size(); ++i) doors[i].count = 0;
    for (size_t i = 0; i < plates.size(); ++i) {
        Plate& p = plates[i];
        const Island& is = islands[p.island];
        float top = islandTop(is);
        float cx = is.x + p.ox, cz = is.z + p.oz;

        float ddx = px - cx, ddz = pz - cz;
        bool byPlayer = (ddx * ddx + ddz * ddz < 0.81f) && fabsf(py - top) < 0.4f;
        bool byEcho = false;
        if (ghostActive) {
            float ex = gx - cx, ez = gz - cz;
            byEcho = (ex * ex + ez * ez < 0.81f) && fabsf(gy - top) < 0.5f;
        }

        if (byPlayer || byEcho) {
            if (!p.active)
                showMsg(byEcho && !byPlayer ? "The Echo is holding the plate - the door is open!"
                                            : "Plate pressed - the door is opening!");
            p.active = true;
            p.timer = p.hold;
        } else {
            if (p.timer > 0.0f) p.timer -= dt;
            if (p.timer <= 0.0f) p.active = false;
        }

        float tp = p.active ? 1.0f : 0.0f;
        p.press += (tp - p.press) * (dt * 10.0f > 1.0f ? 1.0f : dt * 10.0f);
        if (p.active) doors[p.door].count++;
    }
    for (size_t i = 0; i < doors.size(); ++i) {
        Door& d = doors[i];
        d.target = (d.count >= d.needed) ? 1.0f : 0.0f;
        if (d.open < d.target) { d.open += 2.5f * dt; if (d.open > d.target) d.open = d.target; }
        if (d.open > d.target) { d.open -= 2.5f * dt; if (d.open < d.target) d.open = d.target; }
    }

    // ---- checkpoints
    for (size_t i = 0; i < checkpoints.size(); ++i) {
        if ((int)i == curCP) continue;
        const Checkpoint& c = checkpoints[i];
        const Island& is = islands[c.island];
        float ddx = px - (is.x + c.ox), ddz = pz - (is.z + c.oz);
        if (ddx * ddx + ddz * ddz < 1.2f * 1.2f && fabsf(py - islandTop(is)) < 1.5f) {
            curCP = (int)i;
            checkpoints[i].active = true;
            health = MAX_HEALTH;
            spawnBurst(is.x + c.ox, islandTop(is) + 1.0f, is.z + c.oz, 0.3f, 1.0f, 0.5f, 24);
            showMsg("Checkpoint saved - health restored");
        }
    }

    // ---- portal (opens when enough crystals are collected)
    if (portalReady) {
        const Island& pis = islands[portal.island];
        float pcx = pis.x + portal.ox, pcz = pis.z + portal.oz, ptop = islandTop(pis);
        bool wasOpen = portal.open;
        portal.open = crystalsGot >= levelNeeded;
        if (portal.open && !wasOpen) {
            showMsg("The portal is open! Reach it to finish the level");
            spawnBurst(pcx, ptop + 1.7f, pcz, 0.4f, 1.0f, 0.9f, 40);
        }
        if (portal.open && frand(0.0f, 1.0f) < 0.6f) {
            Particle p;
            p.x = pcx + frand(-0.9f, 0.9f); p.y = ptop + frand(0.4f, 3.0f); p.z = pcz + frand(-0.9f, 0.9f);
            p.vx = 0.0f; p.vy = 2.0f; p.vz = 0.0f;
            p.life = 0.7f; p.r = 0.4f; p.g = 1.0f; p.b = 0.9f;
            particles.push_back(p);
        }
        float ddx = px - pcx, ddz = pz - pcz;
        if (ddx * ddx + ddz * ddz < 1.5f * 1.5f && fabsf(py - ptop) < 1.6f) {
            if (portal.open) {
                completeLevel();
                return;
            } else {
                char buf[64];
                sprintf(buf, "The portal is locked - collect %d more crystal(s)", levelNeeded - crystalsGot);
                showMsg(buf);
            }
        }
    }

    updateParticles(dt);
}

// ---------------------------------------------------------------- echo clone
static void recordSample() {
    Sample s;
    s.t = timeSec; s.x = px; s.y = py; s.z = pz;
    s.heading = heading;
    float hs = sqrtf(vx * vx + vz * vz) / MOVE_SPEED;
    s.speed = hs > 1.0f ? 1.0f : hs;
    s.walk = walkPhase;
    s.grounded = grounded;
    recBuf.push_back(s);
    while (!recBuf.empty() && timeSec - recBuf.front().t > ECHO_WINDOW) recBuf.pop_front();
}

static void spawnGhost() {
    if (echoCooldown > 0.0f) return;
    if (recBuf.size() < 2 || recBuf.back().t - recBuf.front().t < 1.0f) {
        showMsg("Echo needs at least 1 second of movement - move around first");
        return;
    }
    ghostTrack.assign(recBuf.begin(), recBuf.end());
    float t0 = ghostTrack[0].t;
    for (size_t i = 0; i < ghostTrack.size(); ++i) ghostTrack[i].t -= t0;

    ghostActive = true; ghostDone = false;
    ghostClock = 0.0f; ghostHold = 0.0f; ghostAge = 0.0f; ghostIdx = 0;
    gx = ghostTrack[0].x; gy = ghostTrack[0].y; gz = ghostTrack[0].z;
    gHead = ghostTrack[0].heading; gSpeed = 0.0f; gWalk = ghostTrack[0].walk;
    gGround = ghostTrack[0].grounded;
    echoCooldown = 0.6f;

    spawnBurst(gx, gy + 1.0f, gz, 0.4f, 0.9f, 1.0f, 26);
    showMsg("Echo clone created - it replays your last moves");
}

static void updateGhost(float dt) {
    if (echoCooldown > 0.0f) echoCooldown -= dt;
    if (!ghostActive) return;
    ghostAge += dt;

    float D = ghostTrack.back().t;
    if (!ghostDone) {
        ghostClock += dt * GHOST_SPEED;
        if (ghostClock >= D) { ghostClock = D; ghostDone = true; }
        while (ghostIdx + 2 < ghostTrack.size() && ghostTrack[ghostIdx + 1].t <= ghostClock) ghostIdx++;

        const Sample& a = ghostTrack[ghostIdx];
        const Sample& b = ghostTrack[ghostIdx + 1 < ghostTrack.size() ? ghostIdx + 1 : ghostIdx];
        float span = b.t - a.t;
        float u = (span > 0.0001f) ? (ghostClock - a.t) / span : 0.0f;
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;

        gx = lerpf(a.x, b.x, u); gy = lerpf(a.y, b.y, u); gz = lerpf(a.z, b.z, u);
        float d = b.heading - a.heading;
        while (d >  180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        gHead  = a.heading + d * u;
        gSpeed = lerpf(a.speed, b.speed, u);
        gWalk  = lerpf(a.walk, b.walk, u);
        gGround = a.grounded;
    } else {
        const Sample& e = ghostTrack.back();
        gx = e.x; gy = e.y; gz = e.z; gHead = e.heading;
        gSpeed = 0.0f; gGround = e.grounded;
        ghostHold += dt;
        if (ghostHold > GHOST_HOLD) {
            ghostActive = false;
            spawnBurst(gx, gy + 1.0f, gz, 0.4f, 0.9f, 1.0f, 20);
            showMsg("The Echo clone faded away");
            return;
        }
    }

    // islands bob up and down, so keep the clone on the surface it stands on
    if (gGround) {
        float best = -1000.0f;
        bool found = false;
        for (size_t i = 0; i < solids.size(); ++i) {
            const Solid& s = solids[i];
            if (fabsf(gx - s.x) <= s.sx * 0.5f + 0.15f && fabsf(gz - s.z) <= s.sz * 0.5f + 0.15f &&
                s.top <= gy + 0.8f && s.top >= gy - 0.8f && s.top > best) {
                best = s.top;
                found = true;
            }
        }
        if (found) gy = best;
    }
}

static void updateEffects(float dt) {
    if (promoTimer > 0.0f)   promoTimer -= dt;
    if (camShake > 0.0f)     { camShake -= 2.2f * dt;     if (camShake < 0.0f) camShake = 0.0f; }
    if (fadeAlpha > 0.0f)    { fadeAlpha -= 1.3f * dt;    if (fadeAlpha < 0.0f) fadeAlpha = 0.0f; }
    if (crystalPulse > 0.0f) { crystalPulse -= 2.5f * dt; if (crystalPulse < 0.0f) crystalPulse = 0.0f; }
    if (ripple >= 0.0f)      { ripple += 1.1f * dt;       if (ripple > 1.0f) ripple = -1.0f; }

    float k = 10.0f * dt; if (k > 1.0f) k = 1.0f;
    squash += (0.0f - squash) * k;

    // sparkles around crystals that are still there
    if (gameState == STATE_PLAYING && !crystals.empty() && frand(0.0f, 1.0f) < 0.3f) {
        const Crystal& c = crystals[rand() % crystals.size()];
        if (!c.collected)
            emitParticle(c.x + frand(-0.4f, 0.4f), c.y + frand(-0.5f, 0.5f), c.z + frand(-0.4f, 0.4f),
                         0.0f, frand(0.5f, 1.2f), 0.0f, 0.7f, c.r, c.g, c.b);
    }
}

static void updateCamera(float dt) {
    float k = 8.0f * dt; if (k > 1.0f) k = 1.0f;
    float tx = px, ty = py + 1.2f, tz = pz;
    float dTarget = camDist;
    if (gameState == STATE_MENU || gameState == STATE_HOWTO) {      // cinematic overview
        tx = menuFx; ty = menuFy; tz = menuFz; dTarget = menuDist;
        k = 3.0f * dt; if (k > 1.0f) k = 1.0f;
    }
    camTx += (tx - camTx) * k;
    camTy += (ty - camTy) * k;
    camTz += (tz - camTz) * k;
    float kd = 2.5f * dt; if (kd > 1.0f) kd = 1.0f;
    camDistView += (dTarget - camDistView) * kd;
}

// ---------------------------------------------------------------- drawing
static void drawBox(float w, float h, float d) {
    glPushMatrix();
    glScalef(w, h, d);
    glutSolidCube(1.0f);
    glPopMatrix();
}

static void drawWireBox(float w, float h, float d) {
    glPushMatrix();
    glScalef(w, h, d);
    glutWireCube(1.0f);
    glPopMatrix();
}

static void setEmission(float r, float g, float b) {
    GLfloat e[] = { r, g, b, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, e);
}

// All robot colors go through setCol so the same model can be drawn
// as a solid robot (player) or as a glowing hologram (Echo clone).
static bool  gGhostMode  = false;
static float gGhostAlpha = 1.0f;

static void setCol(float r, float g, float b) {
    if (gGhostMode) {
        float s = (r + g + b) / 3.0f;
        glColor4f(0.15f + 0.25f * s, 0.55f + 0.35f * s, 1.0f, gGhostAlpha);
    } else {
        glColor3f(r, g, b);
    }
}

static void drawRobotModel(float x, float y, float z, float head, float swing, float bobY,
                           float er, float eg, float eb) {
    glPushMatrix();
    glTranslatef(x, y + bobY, z);
    glRotatef(head, 0.0f, 1.0f, 0.0f);

    for (int s = -1; s <= 1; s += 2) {          // legs
        glPushMatrix();
        glTranslatef(s * 0.20f, 0.70f, 0.0f);
        glRotatef(s * swing, 1.0f, 0.0f, 0.0f);
        glTranslatef(0.0f, -0.35f, 0.0f);
        setCol(0.25f, 0.30f, 0.42f);
        drawBox(0.24f, 0.70f, 0.28f);
        glPopMatrix();
    }
    for (int s = -1; s <= 1; s += 2) {          // arms
        glPushMatrix();
        glTranslatef(s * 0.52f, 1.42f, 0.0f);
        glRotatef(-s * swing, 1.0f, 0.0f, 0.0f);
        glTranslatef(0.0f, -0.30f, 0.0f);
        setCol(0.70f, 0.74f, 0.85f);
        drawBox(0.16f, 0.60f, 0.16f);
        glPopMatrix();
    }

    glPushMatrix();                              // torso
    glTranslatef(0.0f, 1.10f, 0.0f);
    setCol(0.82f, 0.86f, 0.95f);
    drawBox(0.75f, 0.75f, 0.50f);
    setEmission(er, eg, eb);
    setCol(er, eg, eb);
    glTranslatef(0.0f, 0.05f, 0.26f);
    drawBox(0.30f, 0.20f, 0.04f);
    setEmission(0.0f, 0.0f, 0.0f);
    glPopMatrix();

    glPushMatrix();                              // head
    glTranslatef(0.0f, 1.75f, 0.0f);
    setCol(0.90f, 0.92f, 0.98f);
    drawBox(0.55f, 0.50f, 0.50f);
    setEmission(er, eg, eb);
    setCol(er, eg, eb);
    for (int s = -1; s <= 1; s += 2) {
        glPushMatrix();
        glTranslatef(s * 0.12f, 0.03f, 0.26f);
        drawBox(0.12f, 0.10f * gEyeOpen, 0.04f);
        glPopMatrix();
    }
    setCol(0.5f, 0.5f, 0.6f);
    setEmission(0.0f, 0.0f, 0.0f);
    glPushMatrix();
    glTranslatef(0.0f, 0.36f, 0.0f);
    drawBox(0.04f, 0.22f, 0.04f);
    glPopMatrix();
    setEmission(0.9f, 0.2f, 0.8f);
    setCol(0.9f, 0.2f, 0.8f);
    glTranslatef(0.0f, 0.50f, 0.0f);
    glutSolidSphere(0.07, 10, 10);
    setEmission(0.0f, 0.0f, 0.0f);
    glPopMatrix();

    glPopMatrix();
}

static void drawRobot() {
    if (invuln > 0.0f && ((int)(timeSec * 14.0f)) % 2 == 0) return;   // blink after a hit

    float speed = sqrtf(vx * vx + vz * vz);
    float amount = speed / MOVE_SPEED; if (amount > 1.0f) amount = 1.0f;
    float swing = grounded ? sinf(walkPhase) * 45.0f * amount : 20.0f;
    float bobY  = grounded ? fabsf(sinf(walkPhase)) * 0.06f * amount : 0.0f;

    float er = lerpf(0.1f, 0.9f, dimMix);
    float eg = lerpf(0.9f, 0.3f, dimMix);

    // squash & stretch around the feet, idle breathing, blinking
    float sq = squash;
    if (grounded && speed < 0.5f) sq += 0.012f * sinf(timeSec * 2.4f);
    float sy = 1.0f - sq, sxz = 1.0f + sq * 0.5f;
    gEyeOpen = (fmodf(timeSec + 1.3f, 3.7f) < 0.12f) ? 0.1f : 1.0f;

    gGhostMode = false;
    glPushMatrix();
    glTranslatef(px, py, pz);
    glScalef(sxz, sy, sxz);
    glTranslatef(-px, -py, -pz);
    drawRobotModel(px, py, pz, heading, swing, bobY, er, eg, 1.0f);
    glPopMatrix();
    gEyeOpen = 1.0f;
}

static void drawGhost() {
    if (!ghostActive) return;

    float a = 0.65f + 0.15f * sinf(timeSec * 12.0f);          // hologram flicker
    float inT = ghostAge / 0.4f; if (inT > 1.0f) inT = 1.0f;
    float outT = 1.0f;
    if (ghostDone) {
        float left = GHOST_HOLD - ghostHold;
        if (left < 2.0f) outT = left / 2.0f;
        if (outT < 0.0f) outT = 0.0f;
    }
    a *= inT * outT;

    float swing = gGround ? sinf(gWalk) * 45.0f * gSpeed : 20.0f;
    float bobY  = gGround ? fabsf(sinf(gWalk)) * 0.06f * gSpeed : 0.0f;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    gGhostMode = true;
    gGhostAlpha = a;
    drawRobotModel(gx, gy, gz, gHead, swing, bobY, 0.6f, 0.95f, 1.0f);
    gGhostMode = false;

    // pulsing ring on the floor under the clone
    float rr = 0.7f + 0.1f * sinf(timeSec * 5.0f);
    glLineWidth(2.0f);
    glColor4f(0.4f, 0.9f, 1.0f, a * 0.9f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 24; ++i) {
        float ang = i * 6.2832f / 24.0f;
        glVertex3f(gx + cosf(ang) * rr, gy + 0.05f, gz + sinf(ang) * rr);
    }
    glEnd();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static void drawShadow() {
    bool found = false;
    float best = -1000.0f;
    for (size_t i = 0; i < solids.size(); ++i) {
        const Solid& s = solids[i];
        if (fabsf(px - s.x) <= s.sx * 0.5f && fabsf(pz - s.z) <= s.sz * 0.5f &&
            s.top <= py + 0.3f && s.top > best) {
            best = s.top;
            found = true;
        }
    }
    if (!found) return;

    float h = py - best;
    float alpha = 0.5f - h * 0.04f;
    if (alpha < 0.12f) alpha = 0.12f;
    float rad = 0.6f - h * 0.02f;
    if (rad < 0.3f) rad = 0.3f;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glColor4f(0.0f, 0.0f, 0.0f, alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(px, best + 0.03f, pz);
    for (int i = 0; i <= 20; ++i) {
        float a = i * 6.2832f / 20.0f;
        glVertex3f(px + cosf(a) * rad, best + 0.03f, pz + sinf(a) * rad);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static void drawIsland(const Island& is) {
    float tr = 1.0f + 0.35f * dimMix;
    float tg = 1.0f - 0.30f * dimMix;
    float tb = 1.0f + 0.30f * dimMix;

    glPushMatrix();
    glTranslatef(is.x, islandTop(is), is.z);

    glColor3f(clamp1(is.r * tr), clamp1(is.g * tg), clamp1(is.b * tb));
    glPushMatrix();
    glTranslatef(0.0f, -0.25f, 0.0f);
    glScalef(is.sx, 0.5f, is.sz);
    glutSolidCube(1.0f);
    glPopMatrix();

    glColor3f(0.35f, 0.25f, 0.18f);
    glPushMatrix();
    glTranslatef(0.0f, -0.75f, 0.0f);
    glScalef(is.sx * 0.95f, 0.5f, is.sz * 0.95f);
    glutSolidCube(1.0f);
    glPopMatrix();

    glColor3f(0.30f, 0.28f, 0.34f);
    glPushMatrix();
    glTranslatef(0.0f, -1.0f, 0.0f);
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    glScalef(is.sx * 0.62f, is.sz * 0.62f, 3.5f);
    glutSolidCone(1.0f, 1.0f, 8, 2);
    glPopMatrix();

    glPopMatrix();
}

static void drawBridge(const Bridge& b) {
    float cr, cg, cb;
    if (b.dim == 0) { cr = 0.10f; cg = 0.85f; cb = 1.00f; }
    else            { cr = 0.85f; cg = 0.25f; cb = 1.00f; }
    float pulse = 0.75f + 0.25f * sinf(timeSec * 3.0f + b.x + b.z);

    glPushMatrix();
    glTranslatef(b.x, b.y - 0.2f, b.z);

    if (b.fade > 0.02f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (b.fade < 0.99f) glDepthMask(GL_FALSE);
        setEmission(cr * 0.35f * pulse, cg * 0.35f * pulse, cb * 0.35f * pulse);
        glColor4f(cr * 0.6f, cg * 0.6f, cb * 0.6f, b.fade);
        drawBox(b.sx, 0.4f, b.sz);
        setEmission(0.0f, 0.0f, 0.0f);

        glDisable(GL_LIGHTING);
        glLineWidth(2.0f);
        glColor4f(cr, cg, cb, b.fade);
        drawWireBox(b.sx * 1.005f, 0.4f * 1.005f, b.sz * 1.005f);
        glEnable(GL_LIGHTING);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    if (b.fade < 0.98f) {
        float g = 1.0f - b.fade;
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glColor4f(cr, cg, cb, 0.07f * g * pulse);
        drawBox(b.sx, 0.4f, b.sz);
        glLineWidth(1.0f);
        glColor4f(cr, cg, cb, 0.45f * g * pulse);
        drawWireBox(b.sx, 0.4f, b.sz);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }

    glPopMatrix();
}

static void drawDoor(const Door& d) {
    if (d.fade < 0.02f) return;
    float lift = d.open * (d.h + 0.1f);
    float alpha = d.fade * (1.0f - 0.75f * d.open);
    float cr = lerpf(1.0f, 0.2f, d.open);
    float cg = lerpf(0.2f, 1.0f, d.open);
    float cb = 0.25f;

    glPushMatrix();
    glTranslatef(d.x, d.y + lift + d.h * 0.5f, d.z);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    setEmission(cr * 0.5f, cg * 0.5f, cb * 0.5f);
    glColor4f(cr, cg, cb, alpha * 0.55f);
    drawBox(d.sx, d.h, d.sz);
    setEmission(0.0f, 0.0f, 0.0f);

    glDisable(GL_LIGHTING);
    glLineWidth(2.5f);
    glColor4f(cr, cg, cb, alpha);
    drawWireBox(d.sx * 1.01f, d.h * 1.01f, d.sz * 1.01f);
    glEnable(GL_LIGHTING);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glPopMatrix();
}

static void drawPlate(const Plate& p) {
    const Island& is = islands[p.island];
    float x = is.x + p.ox, z = is.z + p.oz, y = islandTop(is);
    float a = p.press;

    glPushMatrix();
    glTranslatef(x, y, z);

    glColor3f(0.20f, 0.20f, 0.25f);               // base
    glPushMatrix();
    glTranslatef(0.0f, 0.04f, 0.0f);
    drawBox(1.8f, 0.08f, 1.8f);
    glPopMatrix();

    float cr = lerpf(1.0f, 0.2f, a), cg = lerpf(0.55f, 1.0f, a), cb = lerpf(0.1f, 0.4f, a);
    setEmission(cr * 0.6f, cg * 0.6f, cb * 0.6f);  // plate
    glColor3f(cr, cg, cb);
    glPushMatrix();
    glTranslatef(0.0f, 0.12f - a * 0.05f, 0.0f);
    drawBox(1.4f, 0.12f, 1.4f);
    glPopMatrix();
    setEmission(0.0f, 0.0f, 0.0f);

    glPopMatrix();
}

static void drawCheckpoint(const Checkpoint& c, bool isCurrent) {
    const Island& is = islands[c.island];
    float x = is.x + c.ox, z = is.z + c.oz, y = islandTop(is);
    float r, g, b;
    if (c.active) { r = 0.3f; g = 1.0f; b = 0.5f; } else { r = 0.6f; g = 0.6f; b = 0.7f; }
    float pulse = 0.6f + 0.4f * sinf(timeSec * 3.0f + x);

    // pad
    glDisable(GL_LIGHTING);
    glLineWidth(isCurrent ? 3.0f : 2.0f);
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 24; ++i) {
        float a = i * 6.2832f / 24.0f;
        glVertex3f(x + cosf(a) * 0.9f, y + 0.04f, z + sinf(a) * 0.9f);
    }
    glEnd();
    glEnable(GL_LIGHTING);

    // expanding pulse ring on activated checkpoints
    if (c.active) {
        float rp = fmodf(timeSec * 0.7f + x * 0.1f, 1.0f);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glLineWidth(2.0f);
        glColor4f(r, g, b, (1.0f - rp) * (isCurrent ? 0.7f : 0.3f));
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 28; ++i) {
            float a = i * 6.2832f / 28.0f;
            glVertex3f(x + cosf(a) * (0.9f + rp * 1.3f), y + 0.05f, z + sinf(a) * (0.9f + rp * 1.3f));
        }
        glEnd();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }

    // floating orb
    glPushMatrix();
    glTranslatef(x, y + 2.4f + sinf(timeSec * 2.0f + x) * 0.15f, z);
    glRotatef(timeSec * 90.0f, 0.0f, 1.0f, 0.0f);
    setEmission(r * pulse, g * pulse, b * pulse);
    glColor3f(r, g, b);
    glutSolidSphere(0.22, 14, 14);
    setEmission(0.0f, 0.0f, 0.0f);
    glPopMatrix();
}

static void drawPortal() {
    if (!portalReady) return;
    const Island& is = islands[portal.island];
    float x = is.x + portal.ox, z = is.z + portal.oz, y = islandTop(is) + 1.7f;
    float r, g, b;
    if (portal.open) { r = 0.3f; g = 1.0f; b = 0.9f; }
    else             { r = 1.0f; g = 0.25f; b = 0.25f; }
    float pulse = 0.7f + 0.3f * sinf(timeSec * 4.0f);

    glPushMatrix();
    glTranslatef(x, y, z);

    {   // vertical light beam, so the portal is visible from far away
        float ba = portal.open ? 0.35f : 0.12f;
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        for (int k = 0; k < 2; ++k) {
            glPushMatrix();
            glRotatef(k * 90.0f, 0.0f, 1.0f, 0.0f);
            glBegin(GL_QUADS);
            glColor4f(r, g, b, ba * pulse);
            glVertex3f(-0.7f, -1.7f, 0.0f); glVertex3f(0.7f, -1.7f, 0.0f);
            glColor4f(r, g, b, 0.0f);
            glVertex3f(0.7f, 26.0f, 0.0f);  glVertex3f(-0.7f, 26.0f, 0.0f);
            glEnd();
            glPopMatrix();
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }

    glRotatef(atan2f(eyeX - x, eyeZ - z) * 180.0f / (float)M_PI, 0.0f, 1.0f, 0.0f);   // face the camera

    // outer ring
    setEmission(r * pulse, g * pulse, b * pulse);
    glColor3f(r, g, b);
    glutSolidTorus(0.12, 1.3, 12, 40);
    setEmission(0.0f, 0.0f, 0.0f);

    // inner glow disc
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    float baseA = portal.open ? 0.35f : 0.10f;
    glColor4f(r, g, b, baseA * pulse);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0.0f, 0.0f, 0.0f);
    for (int i = 0; i <= 32; ++i) {
        float a = i * 6.2832f / 32.0f;
        glVertex3f(cosf(a) * 1.25f, sinf(a) * 1.25f, 0.0f);
    }
    glEnd();

    // spinning swirl arms
    glRotatef(timeSec * (portal.open ? 160.0f : 30.0f), 0.0f, 0.0f, 1.0f);
    glLineWidth(2.0f);
    glColor4f(r, g, b, portal.open ? 0.8f : 0.25f);
    for (int arm = 0; arm < 4; ++arm) {
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= 14; ++i) {
            float t = i / 14.0f;
            float a = arm * 1.5708f + t * 2.6f;
            glVertex3f(cosf(a) * t * 1.2f, sinf(a) * t * 1.2f, 0.0f);
        }
        glEnd();
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);

    glPopMatrix();
}

static void drawLaser(const Laser& l) {
    if (l.fade < 0.02f) return;
    float cr, cg, cb;
    if (l.dim == 1) { cr = 1.0f; cg = 0.2f; cb = 0.75f; }
    else            { cr = 1.0f; cg = 0.15f; cb = 0.1f; }

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    // emitters
    glColor4f(cr, cg, cb, l.fade);
    glPushMatrix(); glTranslatef(l.x1, l.y, l.z1); glutSolidCube(0.35); glPopMatrix();
    glPushMatrix(); glTranslatef(l.x2, l.y, l.z2); glutSolidCube(0.35); glPopMatrix();

    if (l.on) {
        float flick = 0.8f + 0.2f * sinf(timeSec * 40.0f);
        glLineWidth(10.0f);
        glColor4f(cr, cg, cb, 0.25f * l.fade);
        glBegin(GL_LINES); glVertex3f(l.x1, l.y, l.z1); glVertex3f(l.x2, l.y, l.z2); glEnd();
        glLineWidth(3.0f);
        glColor4f(1.0f, 0.85f, 0.85f, 0.95f * l.fade * flick);
        glBegin(GL_LINES); glVertex3f(l.x1, l.y, l.z1); glVertex3f(l.x2, l.y, l.z2); glEnd();
    } else {
        float a = l.warn ? (((int)(timeSec * 16.0f)) % 2 ? 0.55f : 0.15f) : 0.15f;
        glLineWidth(1.0f);
        glColor4f(cr, cg, cb, a * l.fade);
        glBegin(GL_LINES); glVertex3f(l.x1, l.y, l.z1); glVertex3f(l.x2, l.y, l.z2); glEnd();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static void drawOctahedron() {
    const float k = 0.57735f;
    glBegin(GL_TRIANGLES);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                glNormal3f(sx * k, sy * k, sz * k);
                glVertex3f(sx * 1.0f, 0.0f, 0.0f);
                glVertex3f(0.0f, sy * 1.0f, 0.0f);
                glVertex3f(0.0f, 0.0f, sz * 1.0f);
            }
    glEnd();
}

static void drawCrystal(const Crystal& c) {
    if (c.collected) return;
    float bob = sinf(timeSec * 1.6f + c.phase) * 0.25f;
    float spin = timeSec * 60.0f + c.phase * 57.0f;
    float pulse = 0.6f + 0.4f * sinf(timeSec * 3.0f + c.phase);

    glPushMatrix();
    glTranslatef(c.x, c.y + bob, c.z);
    glRotatef(spin, 0.0f, 1.0f, 0.0f);

    setEmission(c.r * pulse, c.g * pulse, c.b * pulse);
    glColor3f(c.r, c.g, c.b);
    glPushMatrix();
    glScalef(0.45f, 0.85f, 0.45f);
    drawOctahedron();
    glPopMatrix();
    setEmission(0.0f, 0.0f, 0.0f);

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glColor4f(c.r, c.g, c.b, 0.12f * pulse);
    glutSolidSphere(0.9, 16, 16);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // lock cage: three orange rings around the crystal (green while it is being unlocked)
    bool solving = (gameState == STATE_PUZZLE && puzzleCrystal >= 0 && &c == &crystals[puzzleCrystal]);
    if (solving && puzzleResult == 1) glColor3f(0.4f, 1.0f, 0.5f);
    else if (solving)                 glColor3f(1.0f, 0.85f, 0.3f);
    else                              glColor3f(1.0f, 0.55f, 0.15f);
    glLineWidth(2.0f);
    for (int k = 0; k < (c.locked ? 3 : 0); ++k) {
        glPushMatrix();
        if (k == 1) glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
        if (k == 2) glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 24; ++i) {
            float a = i * 6.2832f / 24.0f;
            glVertex3f(cosf(a) * 0.75f, sinf(a) * 0.75f, 0.0f);
        }
        glEnd();
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);

    glPopMatrix();
}

static void drawParticles() {
    if (particles.empty()) return;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glPointSize(5.0f);
    glBegin(GL_POINTS);
    for (size_t i = 0; i < particles.size(); ++i) {
        const Particle& p = particles[i];
        glColor4f(p.r, p.g, p.b, clamp1(p.life * 1.5f));
        glVertex3f(p.x, p.y, p.z);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static void drawStars() {
    glPushMatrix();
    glTranslatef(eyeX, eyeY, eyeZ);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (size_t i = 0; i < stars.size(); ++i) {
        float tw = 0.6f + 0.4f * sinf(timeSec * 2.0f + (float)i);
        glColor3f(tw, tw * (1.0f - 0.35f * dimMix), tw);
        glVertex3f(stars[i].x, stars[i].y, stars[i].z);
    }
    glEnd();
    glEnable(GL_FOG);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

static void setupLights() {
    GLfloat l0pos[]  = { -0.5f, 1.0f, 0.4f, 0.0f };
    GLfloat l0diff[] = { lerpf(0.85f, 0.90f, dimMix), lerpf(0.85f, 0.70f, dimMix), lerpf(0.95f, 1.00f, dimMix), 1.0f };
    GLfloat l0amb[]  = { lerpf(0.20f, 0.30f, dimMix), lerpf(0.22f, 0.16f, dimMix), lerpf(0.32f, 0.38f, dimMix), 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, l0pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  l0diff);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  l0amb);

    GLfloat l1pos[]  = { px + 8.0f * cosf(timeSec * 0.5f), py + 6.0f,
                         pz + 8.0f * sinf(timeSec * 0.5f), 1.0f };
    GLfloat l1diff[] = { lerpf(0.2f, 0.9f, dimMix), lerpf(0.7f, 0.3f, dimMix), 0.9f, 1.0f };
    glLightfv(GL_LIGHT1, GL_POSITION, l1pos);
    glLightfv(GL_LIGHT1, GL_DIFFUSE,  l1diff);
    glLightf(GL_LIGHT1, GL_LINEAR_ATTENUATION, 0.04f);
}

// ---------------------------------------------------------------- HUD
static void drawText(float x, float y, void* font, const char* s) {
    glRasterPos2f(x, y);
    for (; *s; ++s) glutBitmapCharacter(font, *s);
}

static void drawRect(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y);
    glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

static void drawCentered(float y, void* font, const char* s) {
    int w = glutBitmapLength(font, (const unsigned char*)s);
    drawText(((float)winW - w) * 0.5f, y, font, s);
}

// ---------------------------------------------------------------- HUD helpers (Step 7)
static void accentColor(float a) {
    glColor4f(lerpf(0.20f, 0.85f, dimMix), lerpf(0.85f, 0.35f, dimMix), 1.0f, a);
}

static void drawPanel(float x, float y, float w, float h, float alpha) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.02f, 0.03f, 0.09f, alpha);
    drawRect(x, y, w, h);
    glLineWidth(1.5f);
    accentColor(0.8f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_BLEND);
}

static void drawBar(float x, float y, float w, float h, float frac, float r, float g, float b) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    glColor3f(0.08f, 0.09f, 0.14f);
    drawRect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f);
    glBegin(GL_QUADS);
    glColor3f(r * 0.55f, g * 0.55f, b * 0.55f);
    glVertex2f(x, y); glVertex2f(x + w * frac, y);
    glColor3f(r, g, b);
    glVertex2f(x + w * frac, y + h); glVertex2f(x, y + h);
    glEnd();
}

static void drawDiamond(float cx, float cy, float s, float r, float g, float b) {
    glColor3f(r, g, b);
    glBegin(GL_POLYGON);
    glVertex2f(cx, cy + s); glVertex2f(cx + s * 0.7f, cy);
    glVertex2f(cx, cy - s); glVertex2f(cx - s * 0.7f, cy);
    glEnd();
}

// ---- mini-map (top-down view of the level)
static float mmScale = 1.0f, mmOffX = 0.0f, mmOffY = 0.0f;
static float MX(float X) { return mmOffX + (X - mapMinX) * mmScale; }
static float MY(float Z) { return mmOffY + (mapMaxZ - Z) * mmScale; }
static void mmRect(float cx, float cz, float sx, float sz) {
    drawRect(MX(cx) - sx * mmScale * 0.5f, MY(cz) - sz * mmScale * 0.5f, sx * mmScale, sz * mmScale);
}

static void drawMinimap(float x0, float y0, float w, float h) {
    drawPanel(x0 - 6.0f, y0 - 6.0f, w + 12.0f, h + 12.0f, 0.55f);
    float spanX = mapMaxX - mapMinX, spanZ = mapMaxZ - mapMinZ;
    if (spanX < 1.0f) spanX = 1.0f;
    if (spanZ < 1.0f) spanZ = 1.0f;
    mmScale = (w / spanX < h / spanZ) ? w / spanX : h / spanZ;
    mmOffX = x0 + (w - spanX * mmScale) * 0.5f;
    mmOffY = y0 + (h - spanZ * mmScale) * 0.5f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (size_t i = 0; i < islands.size(); ++i) {                    // islands
        const Island& is = islands[i];
        if (is.y > 4.5f) continue;
        glColor4f(lerpf(0.25f, 0.42f, dimMix), lerpf(0.38f, 0.26f, dimMix), 0.60f, 0.9f);
        mmRect(is.x, is.z, is.sx, is.sz);
    }
    for (size_t i = 0; i < bridges.size(); ++i) {                    // bridges
        const Bridge& b = bridges[i];
        bool on = b.fade > 0.5f;
        if (b.dim == 0) glColor4f(0.10f, 0.85f, 1.0f, on ? 0.95f : 0.20f);
        else            glColor4f(0.85f, 0.25f, 1.0f, on ? 0.95f : 0.20f);
        mmRect(b.x, b.z, b.sx, b.sz);
    }
    for (size_t i = 0; i < doors.size(); ++i) {                      // doors
        const Door& d = doors[i];
        if (d.fade <= 0.5f) continue;
        if (d.open < 0.5f) glColor4f(1.0f, 0.25f, 0.2f, 1.0f); else glColor4f(0.3f, 1.0f, 0.4f, 1.0f);
        mmRect(d.x, d.z, d.sx > 0.9f ? d.sx : 0.9f, d.sz > 0.9f ? d.sz : 0.9f);
    }
    for (size_t i = 0; i < plates.size(); ++i) {                     // plates
        const Plate& p = plates[i];
        const Island& is = islands[p.island];
        if (p.active) glColor4f(0.3f, 1.0f, 0.4f, 1.0f); else glColor4f(1.0f, 0.6f, 0.15f, 1.0f);
        mmRect(is.x + p.ox, is.z + p.oz, 1.4f, 1.4f);
    }
    for (size_t i = 0; i < checkpoints.size(); ++i) {                // checkpoints
        const Checkpoint& c = checkpoints[i];
        const Island& is = islands[c.island];
        if (c.active) glColor4f(0.3f, 1.0f, 0.5f, 1.0f); else glColor4f(0.6f, 0.6f, 0.7f, 1.0f);
        mmRect(is.x + c.ox, is.z + c.oz, 1.0f, 1.0f);
    }

    glLineWidth(2.0f);                                               // active lasers
    glBegin(GL_LINES);
    for (size_t i = 0; i < lasers.size(); ++i) {
        const Laser& l = lasers[i];
        if (l.fade > 0.5f && l.on) {
            glColor4f(1.0f, 0.2f, 0.2f, 1.0f);
            glVertex2f(MX(l.x1), MY(l.z1)); glVertex2f(MX(l.x2), MY(l.z2));
        }
    }
    glEnd();

    glPointSize(3.0f);                                               // crystals
    glBegin(GL_POINTS);
    for (size_t i = 0; i < crystals.size(); ++i) {
        const Crystal& c = crystals[i];
        if (c.collected) continue;
        if (c.locked) glColor4f(1.0f, 0.6f, 0.15f, 1.0f); else glColor4f(c.r, c.g, c.b, 1.0f);
        glVertex2f(MX(c.x), MY(c.z));
    }
    glEnd();

    if (portalReady) {                                               // portal
        const Island& is = islands[portal.island];
        float cx = MX(is.x + portal.ox), cy = MY(is.z + portal.oz);
        glLineWidth(2.0f);
        if (portal.open) glColor4f(0.3f, 1.0f, 0.9f, 1.0f); else glColor4f(1.0f, 0.3f, 0.3f, 1.0f);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 14; ++i) {
            float a = i * 6.2832f / 14.0f;
            glVertex2f(cx + cosf(a) * 5.0f, cy + sinf(a) * 5.0f);
        }
        glEnd();
    }

    if (ghostActive) {                                               // echo clone
        glPointSize(6.0f);
        glBegin(GL_POINTS);
        glColor4f(0.4f, 0.9f, 1.0f, 1.0f);
        glVertex2f(MX(gx), MY(gz));
        glEnd();
    }

    {                                                                // player arrow
        float h = heading * (float)M_PI / 180.0f;
        float dx = sinf(h), dy = -cosf(h);
        float cx = MX(px), cy = MY(pz);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_TRIANGLES);
        glVertex2f(cx + dx * 6.0f, cy + dy * 6.0f);
        glVertex2f(cx - dx * 3.0f - dy * 3.5f, cy - dy * 3.0f + dx * 3.5f);
        glVertex2f(cx - dx * 3.0f + dy * 3.5f, cy - dy * 3.0f - dx * 3.5f);
        glEnd();
    }
    glDisable(GL_BLEND);
}

// ---- score / grade panel (top center, under the timer)
static void drawGradePanel() {
    char buf[96];
    float cx = (float)winW * 0.5f, top = (float)winH;
    int g = gradeIndex(score);
    drawPanel(cx - 130.0f, top - 100.0f, 260.0f, 52.0f, 0.5f);
    sprintf(buf, "%s     SCORE %d", gradeNames[g], score);
    glColor3f(1.0f, 0.9f, 0.5f);
    drawCentered(top - 66.0f, GLUT_BITMAP_HELVETICA_12, buf);
    float frac = 1.0f;
    if (g < NUM_GRADES - 1) frac = (float)(score - gradeNeed[g]) / (float)(gradeNeed[g + 1] - gradeNeed[g]);
    drawBar(cx - 110.0f, top - 94.0f, 220.0f, 8.0f, frac, 1.0f, 0.8f, 0.2f);
}

// ---- in-game HUD
static void drawGameHUD() {
    char buf[128];
    float top = (float)winH;

    // left panel: level, dimension badges, echo clone
    drawPanel(8.0f, top - 134.0f, 340.0f, 126.0f, 0.5f);
    sprintf(buf, "LEVEL %d: %s", curLevel + 1, levelNames[curLevel]);
    accentColor(1.0f);
    drawText(18.0f, top - 30.0f, GLUT_BITMAP_HELVETICA_18, buf);

    for (int d = 0; d < 2; ++d) {
        float bx = 18.0f + d * 118.0f, by = top - 62.0f;
        float cr = (d == 0) ? 0.20f : 0.85f, cg = (d == 0) ? 0.85f : 0.35f;
        bool on = (dimension == d);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(cr, cg, 1.0f, on ? 0.85f : 0.12f);
        drawRect(bx, by, 108.0f, 20.0f);
        glDisable(GL_BLEND);
        if (on) glColor3f(0.02f, 0.04f, 0.10f); else glColor3f(cr * 0.8f, cg * 0.8f, 0.9f);
        drawText(bx + (d == 0 ? 26.0f : 38.0f), by + 6.0f, GLUT_BITMAP_HELVETICA_12, d == 0 ? "REALITY" : "ECHO");
    }

    float mem = (recBuf.size() > 1) ? recBuf.back().t - recBuf.front().t : 0.0f;
    if (mem > ECHO_WINDOW) mem = ECHO_WINDOW;
    if (ghostActive) {
        if (!ghostDone) sprintf(buf, "ECHO CLONE: replaying...");
        else            sprintf(buf, "ECHO CLONE: holding  %.1f s", GHOST_HOLD - ghostHold);
    } else {
        sprintf(buf, "ECHO CLONE: ready  (press Q)");
    }
    glColor3f(0.6f, 0.95f, 1.0f);
    drawText(18.0f, top - 86.0f, GLUT_BITMAP_HELVETICA_12, buf);
    drawBar(18.0f, top - 104.0f, 200.0f, 10.0f, mem / ECHO_WINDOW, 0.3f, 0.85f, 1.0f);
    glColor3f(0.65f, 0.68f, 0.80f);
    drawText(18.0f, top - 124.0f, GLUT_BITMAP_HELVETICA_12, "WASD move  SPACE jump  E dim  Q echo  F unlock");

    // right panel: health, crystals, stats, portal
    float rx = (float)winW - 262.0f;
    drawPanel(rx, top - 134.0f, 254.0f, 126.0f, 0.5f);
    glColor3f(0.9f, 0.9f, 1.0f);
    drawText(rx + 12.0f, top - 28.0f, GLUT_BITMAP_HELVETICA_12, "HEALTH");
    float hp = health / MAX_HEALTH;
    float pulse = (hp < 0.3f) ? 0.65f + 0.35f * sinf(timeSec * 10.0f) : 1.0f;
    drawBar(rx + 12.0f, top - 48.0f, 228.0f, 14.0f, hp,
            lerpf(1.0f, 0.2f, hp) * pulse, lerpf(0.15f, 0.9f, hp) * pulse, 0.25f);

    drawDiamond(rx + 22.0f, top - 74.0f, 9.0f * (1.0f + crystalPulse * 0.5f), 0.4f, 0.95f, 1.0f);
    sprintf(buf, "%d / %d   (need %d)", crystalsGot, (int)crystals.size(), levelNeeded);
    glColor3f(0.5f + 0.5f * crystalPulse, 1.0f, 1.0f);
    drawText(rx + 42.0f, top - 80.0f, GLUT_BITMAP_HELVETICA_18, buf);

    sprintf(buf, "Falls: %d     Defeats: %d", falls, deaths);
    glColor3f(1.0f, 0.8f, 0.4f);
    drawText(rx + 12.0f, top - 100.0f, GLUT_BITMAP_HELVETICA_12, buf);

    if (portalReady) {
        if (portal.open) {
            glColor3f(0.4f, 1.0f, 0.85f);
            drawText(rx + 12.0f, top - 120.0f, GLUT_BITMAP_HELVETICA_12, "PORTAL OPEN - reach it!");
        } else {
            sprintf(buf, "Portal locked: need %d more crystals", levelNeeded - crystalsGot);
            glColor3f(1.0f, 0.5f, 0.5f);
            drawText(rx + 12.0f, top - 120.0f, GLUT_BITMAP_HELVETICA_12, buf);
        }
    }

    // top center: level time
    int secs = (int)levelTime;
    sprintf(buf, "%d:%02d", secs / 60, secs % 60);
    drawPanel((float)winW * 0.5f - 45.0f, top - 40.0f, 90.0f, 30.0f, 0.5f);
    glColor3f(1.0f, 1.0f, 1.0f);
    drawCentered(top - 31.0f, GLUT_BITMAP_HELVETICA_18, buf);

    // top center (below the timer): score + grade
    drawGradePanel();

    // bottom center: timed plate + message
    float maxT = 0.0f;
    for (size_t i = 0; i < plates.size(); ++i) if (plates[i].timer > maxT) maxT = plates[i].timer;
    if (maxT > 0.0f) {
        sprintf(buf, "Door open: %.1f s", maxT);
        glColor3f(0.3f, 1.0f, 0.5f);
        drawCentered(96.0f, GLUT_BITMAP_HELVETICA_18, buf);
    }
    if (msgTimer > 0.0f) {
        int w = glutBitmapLength(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)msgText);
        float cx = (float)winW * 0.5f;
        drawPanel(cx - w * 0.5f - 14.0f, 48.0f, (float)w + 28.0f, 32.0f, 0.55f);
        glColor3f(1.0f, 1.0f, 0.8f);
        drawText(cx - w * 0.5f, 58.0f, GLUT_BITMAP_HELVETICA_18, msgText);
    }

    // prompt when standing next to a locked crystal
    if (gameState == STATE_PLAYING && nearCrystal >= 0 && nearCrystal < (int)crystals.size() &&
        !crystals[nearCrystal].collected) {
        const Crystal& nc = crystals[nearCrystal];
        if (nc.lockTimer > 0.0f) sprintf(buf, "Locked out - wait %.0f s", nc.lockTimer + 0.5f);
        else                     sprintf(buf, "Press  F  to unlock this crystal");
        int w = glutBitmapLength(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)buf);
        float cx = (float)winW * 0.5f;
        drawPanel(cx - w * 0.5f - 16.0f, 138.0f, (float)w + 32.0f, 34.0f, 0.65f);
        glColor3f(1.0f, 0.8f, 0.4f);
        drawText(cx - w * 0.5f, 148.0f, GLUT_BITMAP_HELVETICA_18, buf);
    }

    drawMinimap(16.0f, 16.0f, 190.0f, 125.0f);
}

// ---- menus
static void strokeString(float x, float y, float sc, const char* s) {
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(sc, sc, 1.0f);
    for (; *s; ++s) glutStrokeCharacter(GLUT_STROKE_ROMAN, *s);
    glPopMatrix();
}

static void drawMenuItems(const char* const* items, int count, float yTop) {
    char line[96];
    for (int i = 0; i < count; ++i) {
        bool sel = (i == menuIndex);
        sprintf(line, "%s%s%s", sel ? "> " : "   ", items[i], sel ? " <" : "   ");
        if (sel) glColor3f(1.0f, 0.95f, 0.55f); else glColor3f(0.72f, 0.82f, 0.95f);
        drawCentered(yTop - i * 42.0f, GLUT_BITMAP_TIMES_ROMAN_24, line);
    }
}

static void drawDarken(float a) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.05f, a);
    drawRect(0.0f, 0.0f, (float)winW, (float)winH);
    glDisable(GL_BLEND);
}

static void drawMenuScreen() {
    drawDarken(0.35f);

    const char* t = "ECHO SHIFT";
    float sc = 0.55f * (float)winH / 650.0f;
    float tw = (float)glutStrokeLength(GLUT_STROKE_ROMAN, (const unsigned char*)t) * sc;
    float tx = ((float)winW - tw) * 0.5f;
    float ty = (float)winH * 0.70f + sinf(timeSec * 2.0f) * 3.0f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(10.0f);
    accentColor(0.25f);
    strokeString(tx + 3.0f, ty - 3.0f, sc, t);
    glLineWidth(3.0f);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    strokeString(tx, ty, sc, t);
    glDisable(GL_BLEND);

    accentColor(1.0f);
    drawCentered(ty - 36.0f, GLUT_BITMAP_HELVETICA_18, "T H E   P A R A L L E L   D I M E N S I O N");

    char lvl[64];
    sprintf(lvl, "LEVEL:  < %d - %s >", selectedLevel + 1, levelNames[selectedLevel]);
    const char* items[4] = { "START GAME", lvl, "HOW TO PLAY", "QUIT" };
    drawMenuItems(items, 4, ty - 100.0f);

    glColor3f(0.60f, 0.70f, 0.85f);
    drawCentered(40.0f, GLUT_BITMAP_HELVETICA_12, "UP / DOWN: select     ENTER: confirm     LEFT / RIGHT: change level");
}

static void drawHowTo() {
    drawDarken(0.55f);
    float cx = (float)winW * 0.5f, cy = (float)winH * 0.5f;
    drawPanel(cx - 380.0f, cy - 200.0f, 760.0f, 400.0f, 0.8f);
    accentColor(1.0f);
    drawCentered(cy + 160.0f, GLUT_BITMAP_TIMES_ROMAN_24, "HOW TO PLAY");

    const char* lines[9] = {
        "Move: W A S D     Jump: SPACE     Camera: mouse drag / arrow keys",
        "E: switch Reality (cyan) <-> Echo (purple) - bridges exist in one only",
        "Q: Echo clone - replays your last 5 seconds and holds plates for you",
        "Collect crystals to open the portal, then step into it",
        "Lasers hurt - jump over them or wait until they switch off",
        "Checkpoints save your spot and restore your health",
        "ESC: pause     R: respawn     1 / 2 / 3: choose level",
        "Caged (orange) crystals are locked: press F next to one to solve its puzzle",
        "Solve puzzles (Math / Logic / Tech) to earn SCORE and GRADE - reach GRADUATE to win!"
    };
    glColor3f(0.88f, 0.93f, 1.0f);
    for (int i = 0; i < 9; ++i)
        drawCentered(cy + 108.0f - i * 32.0f, GLUT_BITMAP_HELVETICA_18, lines[i]);
    glColor3f(1.0f, 0.9f, 0.5f);
    drawCentered(cy - 178.0f, GLUT_BITMAP_HELVETICA_12, "Press any key to go back");
}

// ---- crystal puzzle screen
static void drawPadlock(float cx, float cy, float s, float open, float r, float g, float b) {
    // shackle
    glLineWidth(5.0f);
    glColor3f(r, g, b);
    float lift = open * s * 0.35f;
    glBegin(GL_LINE_STRIP);
    glVertex2f(cx - s * 0.38f, cy + s * 0.35f);
    glVertex2f(cx - s * 0.38f, cy + s * 0.55f + lift);
    for (int i = 0; i <= 14; ++i) {
        float a = 3.1416f - i * 3.1416f / 14.0f;
        glVertex2f(cx + cosf(a) * s * 0.38f, cy + s * 0.55f + lift + sinf(a) * s * 0.38f);
    }
    glVertex2f(cx + s * 0.38f, cy + s * 0.35f + (open > 0.5f ? lift : 0.0f));
    glEnd();
    // body
    drawRect(cx - s * 0.6f, cy - s * 0.5f, s * 1.2f, s * 0.9f);
    // keyhole
    glColor3f(0.05f, 0.06f, 0.10f);
    drawRect(cx - s * 0.07f, cy - s * 0.3f, s * 0.14f, s * 0.35f);
}

static bool isWordy(const char* s) {
    int letters = 0;
    for (; *s; ++s) if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')) letters++;
    return letters > 8;
}

static void drawPuzzleScreen() {
    char buf[160];
    drawDarken(0.70f);

    float cx = (float)winW * 0.5f, cy = (float)winH * 0.5f;
    drawPanel(cx - 290.0f, cy - 210.0f, 580.0f, 420.0f, 0.88f);

    float cr = 1.0f, cg = 0.62f, cb = 0.15f, open = 0.0f;
    if (puzzleResult == 1) { cr = 0.4f; cg = 1.0f; cb = 0.5f; open = 1.0f; }
    if (puzzleResult == 2) { cr = 1.0f; cg = 0.25f; cb = 0.2f; }
    drawPadlock(cx, cy + 150.0f, 38.0f, open, cr, cg, cb);

    if (puzzleResult == 1)      glColor3f(0.4f, 1.0f, 0.5f);
    else if (puzzleResult == 2) glColor3f(1.0f, 0.35f, 0.3f);
    else                        glColor3f(1.0f, 0.78f, 0.3f);
    drawCentered(cy + 92.0f, GLUT_BITMAP_TIMES_ROMAN_24,
                 puzzleResult == 1 ? "CRYSTAL UNLOCKED!" : (puzzleResult == 2 ? "LOCKED OUT - try again later" : "CRYSTAL LOCKED"));

    // category + difficulty tag
    if (puzzleResult == 1) {
        sprintf(buf, "+%d points!", puzzleEarned);
        glColor3f(0.5f, 1.0f, 0.6f);
    } else {
        sprintf(buf, "[ %s - %s ]   worth up to %d points", catNames[puzzleCat], diffNames[puzzleDiff], diffBase[puzzleDiff]);
        if (puzzleDiff == 0)      glColor3f(0.5f, 1.0f, 0.6f);
        else if (puzzleDiff == 1) glColor3f(1.0f, 0.9f, 0.4f);
        else                      glColor3f(1.0f, 0.5f, 0.4f);
    }
    drawCentered(cy + 62.0f, GLUT_BITMAP_HELVETICA_18, buf);

    // question
    bool bitmapQ = puzzleMC || isWordy(puzzleQuestion);
    if (bitmapQ) {
        void* f = GLUT_BITMAP_HELVETICA_18;
        if (glutBitmapLength(f, (const unsigned char*)puzzleQuestion) > 540) f = GLUT_BITMAP_HELVETICA_12;
        glColor3f(1.0f, 1.0f, 1.0f);
        drawCentered(puzzleMC ? cy + 34.0f : cy + 6.0f, f, puzzleQuestion);
    } else {
        float sc = 0.30f;
        float w = (float)glutStrokeLength(GLUT_STROKE_ROMAN, (const unsigned char*)puzzleQuestion) * sc;
        if (w > 520.0f) { sc *= 520.0f / w; w = 520.0f; }
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(2.5f);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        strokeString(cx - w * 0.5f, cy + 10.0f, sc, puzzleQuestion);
        glDisable(GL_BLEND);
    }

    int left = PUZZLE_MAX_TRIES - puzzleAttempts;

    if (puzzleMC) {
        for (int i = 0; i < 4; ++i) {
            float oy = cy - 8.0f - i * 34.0f, ox = cx - 220.0f;
            bool off = puzzleOptOff[i];
            bool good = (puzzleResult != 0 && i == puzzleCorrect);
            if (good)     glColor3f(0.10f, 0.35f, 0.18f);
            else if (off) glColor3f(0.12f, 0.12f, 0.14f);
            else          glColor3f(0.06f, 0.08f, 0.15f);
            drawRect(ox, oy, 440.0f, 28.0f);
            glLineWidth(2.0f);
            if (good) glColor3f(0.4f, 1.0f, 0.5f); else if (off) glColor3f(0.3f, 0.3f, 0.35f); else accentColor(1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(ox, oy); glVertex2f(ox + 440.0f, oy); glVertex2f(ox + 440.0f, oy + 28.0f); glVertex2f(ox, oy + 28.0f);
            glEnd();
            sprintf(buf, "%d)  %s", i + 1, puzzleOpt[i]);
            if (off) glColor3f(0.4f, 0.4f, 0.45f); else glColor3f(1.0f, 1.0f, 0.8f);
            drawText(ox + 12.0f, oy + 8.0f, GLUT_BITMAP_HELVETICA_18, buf);
        }
        sprintf(buf, "Attempts left: %d", left);
        glColor3f(left > 1 ? 0.7f : 1.0f, left > 1 ? 0.9f : 0.5f, left > 1 ? 1.0f : 0.4f);
        drawCentered(cy - 140.0f, GLUT_BITMAP_HELVETICA_18, buf);
        if (puzzleHint[0] && puzzleResult == 0) {
            glColor3f(1.0f, 0.9f, 0.5f);
            drawCentered(cy - 166.0f, GLUT_BITMAP_HELVETICA_12, puzzleHint);
        }
        glColor3f(0.60f, 0.70f, 0.85f);
        drawCentered(cy - 196.0f, GLUT_BITMAP_HELVETICA_12, "Press 1 - 4 to choose an answer     ESC: cancel");
    } else {
        float bx = cx - 110.0f, by = cy - 70.0f;
        glColor3f(0.06f, 0.08f, 0.15f);
        drawRect(bx, by, 220.0f, 44.0f);
        glLineWidth(2.0f);
        accentColor(1.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(bx, by); glVertex2f(bx + 220.0f, by); glVertex2f(bx + 220.0f, by + 44.0f); glVertex2f(bx, by + 44.0f);
        glEnd();
        sprintf(buf, "%s%s", puzzleInput, (((int)(timeSec * 2.0f)) % 2 == 0 && puzzleResult == 0) ? "_" : " ");
        glColor3f(1.0f, 1.0f, 0.7f);
        drawCentered(by + 13.0f, GLUT_BITMAP_TIMES_ROMAN_24, buf);

        sprintf(buf, "Attempts left: %d", left);
        glColor3f(left > 1 ? 0.7f : 1.0f, left > 1 ? 0.9f : 0.5f, left > 1 ? 1.0f : 0.4f);
        drawCentered(cy - 104.0f, GLUT_BITMAP_HELVETICA_18, buf);
        if (puzzleHint[0] && puzzleResult == 0) {
            glColor3f(1.0f, 0.9f, 0.5f);
            drawCentered(cy - 132.0f, GLUT_BITMAP_HELVETICA_18, puzzleHint);
        }
        if (puzzleResult == 0 && puzzleAttempts > 0 && puzzleHint[0]) {
            glColor3f(1.0f, 0.45f, 0.4f);
            drawCentered(cy - 160.0f, GLUT_BITMAP_HELVETICA_12, "Wrong answer!");
        }
        glColor3f(0.60f, 0.70f, 0.85f);
        drawCentered(cy - 196.0f, GLUT_BITMAP_HELVETICA_12, "Type the answer     ENTER: submit     BACKSPACE: delete     ESC: cancel");
    }
}

// ---- graduation screen
static void drawGradCap(float cx, float cy, float s) {
    glColor3f(0.10f, 0.12f, 0.22f);                           // cap base
    glBegin(GL_POLYGON);
    glVertex2f(cx - s * 0.45f, cy - s * 0.05f); glVertex2f(cx - s * 0.45f, cy - s * 0.45f);
    glVertex2f(cx, cy - s * 0.68f);             glVertex2f(cx + s * 0.45f, cy - s * 0.45f);
    glVertex2f(cx + s * 0.45f, cy - s * 0.05f);
    glEnd();
    glColor3f(0.16f, 0.19f, 0.34f);                           // board
    glBegin(GL_POLYGON);
    glVertex2f(cx - s, cy); glVertex2f(cx, cy + s * 0.45f); glVertex2f(cx + s, cy); glVertex2f(cx, cy - s * 0.45f);
    glEnd();
    glLineWidth(3.0f);
    glColor3f(1.0f, 0.85f, 0.3f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(cx - s, cy); glVertex2f(cx, cy + s * 0.45f); glVertex2f(cx + s, cy); glVertex2f(cx, cy - s * 0.45f);
    glEnd();
    glBegin(GL_LINES);                                        // tassel
    glVertex2f(cx, cy); glVertex2f(cx + s * 0.8f, cy - s * 0.1f);
    glVertex2f(cx + s * 0.8f, cy - s * 0.1f); glVertex2f(cx + s * 0.8f, cy - s * 0.55f);
    glEnd();
    drawRect(cx + s * 0.8f - 4.0f, cy - s * 0.70f, 8.0f, 16.0f);
}

static void drawGraduation() {
    char buf[128];
    drawDarken(0.70f);
    float cx = (float)winW * 0.5f, cy = (float)winH * 0.5f;
    drawPanel(cx - 330.0f, cy - 230.0f, 660.0f, 460.0f, 0.88f);
    drawGradCap(cx, cy + 165.0f, 60.0f);

    glColor3f(1.0f, 0.85f, 0.4f);
    drawCentered(cy + 78.0f, GLUT_BITMAP_TIMES_ROMAN_24, "CONGRATULATIONS - YOU GRADUATED!");
    glColor3f(0.8f, 0.95f, 1.0f);
    drawCentered(cy + 48.0f, GLUT_BITMAP_HELVETICA_18, "Math + Logic + Tech mastered: you earned the Echo Shift degree.");

    int tot = totalPuzzles + totalWrong;
    int acc = tot > 0 ? (totalPuzzles * 100 / tot) : 100;
    int s2 = (int)totalTime;
    glColor3f(1.0f, 0.9f, 0.5f);
    sprintf(buf, "Final score: %d      Grade: %s", score, gradeNames[gradeIndex(score)]);
    drawCentered(cy + 8.0f, GLUT_BITMAP_HELVETICA_18, buf);
    glColor3f(0.9f, 0.9f, 1.0f);
    sprintf(buf, "Puzzles solved: %d     Wrong answers: %d     Accuracy: %d%%", totalPuzzles, totalWrong, acc);
    drawCentered(cy - 20.0f, GLUT_BITMAP_HELVETICA_18, buf);
    sprintf(buf, "Crystals: %d     Falls: %d     Defeats: %d", totalCrystals, totalFalls, totalDefeats);
    drawCentered(cy - 46.0f, GLUT_BITMAP_HELVETICA_18, buf);
    sprintf(buf, "Total time: %d:%02d", s2 / 60, s2 % 60);
    drawCentered(cy - 72.0f, GLUT_BITMAP_HELVETICA_18, buf);
    glColor3f(1.0f, 0.9f, 0.5f);
    drawCentered(cy - 130.0f, GLUT_BITMAP_HELVETICA_18, "Press ENTER to play again   |   ESC: quit");
}

// ---- master HUD
static void drawHUD() {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, winW, 0, winH);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);

    char buf[128];

    if (gameState == STATE_MENU) {
        drawMenuScreen();
    } else if (gameState == STATE_HOWTO) {
        drawHowTo();
    } else {
        // screen effects: dimension flash, damage flash, low-health vignette, shockwave
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (flash > 0.0f) {
            if (dimension == 0) glColor4f(0.1f, 0.7f, 1.0f, flash * 0.40f);
            else                glColor4f(0.8f, 0.2f, 1.0f, flash * 0.40f);
            drawRect(0.0f, 0.0f, (float)winW, (float)winH);
        }
        if (damageFlash > 0.0f) {
            glColor4f(1.0f, 0.05f, 0.05f, damageFlash * 0.45f);
            drawRect(0.0f, 0.0f, (float)winW, (float)winH);
        }
        if (health <= 25.0f && gameState == STATE_PLAYING) {
            glColor4f(1.0f, 0.0f, 0.0f, 0.06f + 0.05f * sinf(timeSec * 8.0f));
            drawRect(0.0f, 0.0f, (float)winW, (float)winH);
        }
        if (ripple >= 0.0f) {
            float diag = sqrtf((float)(winW * winW + winH * winH));
            float rr = ripple * diag * 0.6f;
            glLineWidth(5.0f);
            accentColor((1.0f - ripple) * 0.8f);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 64; ++i) {
                float a = i * 6.2832f / 64.0f;
                glVertex2f((float)winW * 0.5f + cosf(a) * rr, (float)winH * 0.5f + sinf(a) * rr);
            }
            glEnd();
        }
        glDisable(GL_BLEND);

        drawGameHUD();

        // level banner at the start of a level
        if (gameState == STATE_PLAYING && bannerTimer > 0.0f) {
            float a = bannerTimer > 1.0f ? 1.0f : bannerTimer;
            float cy = (float)winH * 0.72f;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            sprintf(buf, "LEVEL %d", curLevel + 1);
            glColor4f(0.5f, 0.95f, 1.0f, a);
            drawCentered(cy, GLUT_BITMAP_TIMES_ROMAN_24, buf);
            glColor4f(1.0f, 1.0f, 1.0f, a);
            drawCentered(cy - 34.0f, GLUT_BITMAP_TIMES_ROMAN_24, levelNames[curLevel]);
            glColor4f(0.85f, 0.92f, 1.0f, a);
            drawCentered(cy - 66.0f, GLUT_BITMAP_HELVETICA_18, levelHints[curLevel]);
            sprintf(buf, "Collect %d crystals to open the portal", levelNeeded);
            glColor4f(1.0f, 0.9f, 0.6f, a);
            drawCentered(cy - 92.0f, GLUT_BITMAP_HELVETICA_18, buf);
            glColor4f(1.0f, 0.7f, 0.35f, a);
            drawCentered(cy - 118.0f, GLUT_BITMAP_HELVETICA_18, "Caged (orange) crystals are LOCKED: stand close and press F to solve a puzzle");
            glDisable(GL_BLEND);
        }

        // grade promotion banner
        if (promoTimer > 0.0f) {
            float a = promoTimer > 1.0f ? 1.0f : promoTimer;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1.0f, 0.9f, 0.4f, a);
            drawCentered((float)winH * 0.58f, GLUT_BITMAP_TIMES_ROMAN_24, promoText);
            glDisable(GL_BLEND);
        }

        if (gameState == STATE_PAUSED) {
            drawDarken(0.6f);
            float cy = (float)winH * 0.62f;
            accentColor(1.0f);
            drawCentered(cy, GLUT_BITMAP_TIMES_ROMAN_24, "PAUSED");
            const char* items[4] = { "RESUME", "RESTART LEVEL", "MAIN MENU", "QUIT" };
            drawMenuItems(items, 4, cy - 56.0f);
            glColor3f(0.60f, 0.70f, 0.85f);
            drawCentered(40.0f, GLUT_BITMAP_HELVETICA_12, "UP / DOWN: select     ENTER: confirm     ESC: resume");
        }

        if (gameState == STATE_LEVEL_DONE || gameState == STATE_VICTORY) {
            drawDarken(0.62f);
            float cy = (float)winH * 0.66f;
            if (gameState == STATE_LEVEL_DONE) {
                sprintf(buf, "LEVEL %d COMPLETE", curLevel + 1);
                glColor3f(0.4f, 1.0f, 0.85f);
                drawCentered(cy, GLUT_BITMAP_TIMES_ROMAN_24, buf);
                glColor3f(1.0f, 1.0f, 1.0f);
                drawCentered(cy - 32.0f, GLUT_BITMAP_HELVETICA_18, levelNames[curLevel]);
                int s2 = (int)levelTime;
                sprintf(buf, "Time: %d:%02d", s2 / 60, s2 % 60);
                glColor3f(0.9f, 0.9f, 1.0f);
                drawCentered(cy - 74.0f, GLUT_BITMAP_HELVETICA_18, buf);
                sprintf(buf, "Crystals: %d / %d", crystalsGot, (int)crystals.size());
                drawCentered(cy - 98.0f, GLUT_BITMAP_HELVETICA_18, buf);
                sprintf(buf, "Falls: %d     Defeats: %d", falls, deaths);
                drawCentered(cy - 122.0f, GLUT_BITMAP_HELVETICA_18, buf);
                sprintf(buf, "Puzzles solved: %d     Wrong answers: %d", puzzlesSolved, puzzlesWrong);
                drawCentered(cy - 146.0f, GLUT_BITMAP_HELVETICA_18, buf);
                sprintf(buf, "Level bonus: +%d     Score: %d     Grade: %s", levelBonus, score, gradeNames[gradeIndex(score)]);
                glColor3f(0.6f, 1.0f, 0.7f);
                drawCentered(cy - 170.0f, GLUT_BITMAP_HELVETICA_18, buf);
                glColor3f(1.0f, 0.9f, 0.5f);
                drawCentered(cy - 198.0f, GLUT_BITMAP_HELVETICA_18, "Press ENTER for the next level");
            } else {
                glColor3f(1.0f, 0.85f, 0.4f);
                drawCentered(cy, GLUT_BITMAP_TIMES_ROMAN_24, "ECHO SHIFT COMPLETE!");
                glColor3f(0.8f, 0.95f, 1.0f);
                drawCentered(cy - 32.0f, GLUT_BITMAP_HELVETICA_18, "You mastered both dimensions and your own Echo.");
                int s2 = (int)totalTime;
                sprintf(buf, "Total time: %d:%02d", s2 / 60, s2 % 60);
                glColor3f(0.9f, 0.9f, 1.0f);
                drawCentered(cy - 74.0f, GLUT_BITMAP_HELVETICA_18, buf);
                sprintf(buf, "Crystals collected: %d", totalCrystals);
                drawCentered(cy - 98.0f, GLUT_BITMAP_HELVETICA_18, buf);
                sprintf(buf, "Falls: %d     Defeats: %d", totalFalls, totalDefeats);
                drawCentered(cy - 122.0f, GLUT_BITMAP_HELVETICA_18, buf);
                sprintf(buf, "Puzzles solved: %d     Wrong answers: %d", totalPuzzles, totalWrong);
                drawCentered(cy - 146.0f, GLUT_BITMAP_HELVETICA_18, buf);
                sprintf(buf, "Final score: %d     Grade: %s  (reach %d to graduate)", score, gradeNames[gradeIndex(score)], gradeNeed[NUM_GRADES - 1]);
                glColor3f(0.6f, 1.0f, 0.7f);
                drawCentered(cy - 170.0f, GLUT_BITMAP_HELVETICA_18, buf);
                glColor3f(1.0f, 0.9f, 0.5f);
                drawCentered(cy - 198.0f, GLUT_BITMAP_HELVETICA_18, "Press ENTER to play again   |   1-3: choose a level   |   ESC: quit");
            }
        }
    }

    if (gameState == STATE_GRADUATED) drawGraduation();
    if (gameState == STATE_PUZZLE) drawPuzzleScreen();

    // fade-in from black
    if (fadeAlpha > 0.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.0f, 0.0f, 0.0f, fadeAlpha);
        drawRect(0.0f, 0.0f, (float)winW, (float)winH);
        glDisable(GL_BLEND);
    }

    glEnable(GL_FOG);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

// ---------------------------------------------------------------- sky, planet, motes
static void drawSkyGradient(float sr, float sg, float sb) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);

    glBegin(GL_QUADS);
    glColor3f(sr * 0.45f, sg * 0.45f, sb * 0.60f);                       // top
    glVertex2f(0.0f, 1.0f); glVertex2f(1.0f, 1.0f);
    glColor3f(clamp1(sr * 1.8f + 0.04f), clamp1(sg * 1.8f + 0.05f), clamp1(sb * 1.5f + 0.08f));   // horizon
    glVertex2f(1.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glEnd();

    glEnable(GL_FOG);
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

static void drawPlanet() {
    glPushMatrix();
    glTranslatef(eyeX - 70.0f, eyeY + 38.0f, eyeZ - 95.0f);
    glDisable(GL_FOG);
    float pr = lerpf(0.30f, 0.70f, dimMix);
    float pg = lerpf(0.50f, 0.30f, dimMix);
    float pb = lerpf(0.95f, 0.90f, dimMix);
    glColor3f(pr, pg, pb);
    glutSolidSphere(24.0, 36, 24);

    glPushMatrix();                                  // ring
    glRotatef(72.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(15.0f, 0.0f, 1.0f, 0.0f);
    glScalef(1.0f, 1.0f, 0.06f);
    glColor3f(clamp1(pr * 1.15f), clamp1(pg * 1.15f), clamp1(pb * 1.1f));
    glutSolidTorus(2.0, 38.0, 8, 64);
    glPopMatrix();

    glEnable(GL_FOG);
    glPopMatrix();
}

static float wrapf(float v, float size) {
    v = fmodf(v, size);
    if (v < 0.0f) v += size;
    return v - size * 0.5f;
}

static void drawMotes() {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glPointSize(2.5f);
    glBegin(GL_POINTS);
    for (int i = 0; i < NUM_MOTES; ++i) {
        float x = camTx + wrapf(motes[i][0] + timeSec * (0.25f + 0.1f * (i % 3)), 56.0f);
        float y = camTy + wrapf(motes[i][1] + timeSec * 0.35f, 30.0f);
        float z = camTz + wrapf(motes[i][2] - timeSec * 0.2f, 56.0f);
        float a = 0.25f + 0.25f * sinf(timeSec * 2.0f + motes[i][3]);
        glColor4f(lerpf(0.5f, 0.9f, dimMix), lerpf(0.9f, 0.5f, dimMix), 1.0f, a);
        glVertex3f(x, y, z);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ---------------------------------------------------------------- callbacks
static void display() {
    float sr = lerpf(skyReal[0], skyEcho[0], dimMix);
    float sg = lerpf(skyReal[1], skyEcho[1], dimMix);
    float sb = lerpf(skyReal[2], skyEcho[2], dimMix);
    glClearColor(sr, sg, sb, 1.0f);
    GLfloat fogCol[] = { sr, sg, sb, 1.0f };
    glFogfv(GL_FOG_COLOR, fogCol);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSkyGradient(sr, sg, sb);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float yaw = camYaw * (float)M_PI / 180.0f;
    float pit = camPitch * (float)M_PI / 180.0f;
    eyeX = camTx + camDistView * cosf(pit) * sinf(yaw);
    eyeY = camTy + camDistView * sinf(pit);
    eyeZ = camTz + camDistView * cosf(pit) * cosf(yaw);
    float shx = sinf(timeSec * 61.0f) * camShake * 0.35f;
    float shy = cosf(timeSec * 47.0f) * camShake * 0.35f;
    gluLookAt(eyeX + shx, eyeY + shy, eyeZ, camTx, camTy, camTz, 0.0, 1.0, 0.0);

    setupLights();

    drawStars();
    drawPlanet();
    for (size_t i = 0; i < islands.size(); ++i)  drawIsland(islands[i]);
    for (size_t i = 0; i < plates.size(); ++i)   drawPlate(plates[i]);
    for (size_t i = 0; i < checkpoints.size(); ++i) drawCheckpoint(checkpoints[i], (int)i == curCP);
    drawShadow();
    drawRobot();
    for (size_t i = 0; i < crystals.size(); ++i) drawCrystal(crystals[i]);

    // transparent things last
    for (size_t i = 0; i < bridges.size(); ++i)  drawBridge(bridges[i]);
    for (size_t i = 0; i < doors.size(); ++i)    drawDoor(doors[i]);
    for (size_t i = 0; i < lasers.size(); ++i)   drawLaser(lasers[i]);
    drawPortal();
    drawGhost();
    drawMotes();
    drawParticles();
    drawHUD();

    glutSwapBuffers();
}

static void reshape(int w, int h) {
    if (h == 0) h = 1;
    winW = w; winH = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, (double)w / h, 0.1, 400.0);
    glMatrixMode(GL_MODELVIEW);
}

static void timer(int) {
    static int last = 0;
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (last == 0) ? 0.016f : (now - last) / 1000.0f;
    last = now;
    if (dt > 0.025f) dt = 0.025f;
    timeSec = now / 1000.0f;

    updateDimension(dt);
    if (puzzleCooldown > 0.0f) puzzleCooldown -= dt;
    if (gameState != STATE_PAUSED) updateEffects(dt);

    if (gameState == STATE_PLAYING) {
        updatePlayer(dt);
        updateGhost(dt);
        updateWorld(dt);
        recordSample();
        levelTime += dt;
        if (bannerTimer > 0.0f) bannerTimer -= dt;
        if (graduateDelay > 0.0f && gameState == STATE_PLAYING) {
            graduateDelay -= dt;
            if (graduateDelay <= 0.0f) graduateNow();
        }
    } else if (gameState == STATE_PAUSED) {
        // game is frozen while paused
    } else if (gameState == STATE_PUZZLE) {
        updateParticles(dt);                 // world is frozen, effects keep playing
        updatePuzzle(dt);
    } else {
        updateParticles(dt);
        if (gameState == STATE_MENU || gameState == STATE_HOWTO) {
            camYaw += 8.0f * dt;                                   // slow cinematic orbit
            dimension = ((int)(timeSec / 6.0f)) % 2;               // background morphs between dimensions
        } else {
            camYaw += 12.0f * dt;
            if ((gameState == STATE_VICTORY || gameState == STATE_GRADUATED) && frand(0.0f, 1.0f) < 0.08f)   // fireworks
                spawnBurst(px + frand(-8.0f, 8.0f), py + frand(5.0f, 12.0f), pz + frand(-8.0f, 8.0f),
                           frand(0.3f, 1.0f), frand(0.3f, 1.0f), frand(0.3f, 1.0f), 40);
        }
    }
    updateCamera(dt);

    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

static void menuMove(int d) {
    menuIndex = (menuIndex + d + 4) % 4;
}

static void resetTotals() {
    totalCrystals = totalFalls = totalDefeats = 0;
    totalTime = 0.0f;
    totalPuzzles = totalWrong = 0;
    score = 0; levelStartScore = 0;
}

static void menuSelect() {
    if (gameState == STATE_MENU) {
        if (menuIndex == 0 || menuIndex == 1) { resetTotals(); loadLevel(selectedLevel); }
        else if (menuIndex == 2) gameState = STATE_HOWTO;
        else exit(0);
    } else if (gameState == STATE_PAUSED) {
        if (menuIndex == 0) gameState = STATE_PLAYING;
        else if (menuIndex == 1) { score = levelStartScore; loadLevel(curLevel); }   // restart: this level's points are lost
        else if (menuIndex == 2) { loadLevel(selectedLevel); gameState = STATE_MENU; menuIndex = 0; }
        else exit(0);
    }
}

static void keyDown(unsigned char key, int, int) {
    if (key == 27) {
        if (gameState == STATE_PLAYING)     { gameState = STATE_PAUSED; menuIndex = 0; }
        else if (gameState == STATE_PAUSED) { gameState = STATE_PLAYING; }
        else if (gameState == STATE_HOWTO)  { gameState = STATE_MENU; }
        else if (gameState == STATE_PUZZLE) { if (puzzleResult == 0) closePuzzle(false, 0.0f); }
        else exit(0);
        return;
    }
    if (gameState == STATE_PUZZLE) { puzzleKey(key); return; }
    if (key >= 'A' && key <= 'Z') key = (unsigned char)(key + 32);
    keys[key] = true;

    if (gameState == STATE_HOWTO) { gameState = STATE_MENU; return; }
    if (gameState == STATE_MENU || gameState == STATE_PAUSED) {
        if (key == 'w') menuMove(-1);
        if (key == 's') menuMove(1);
        if (key == 13 || key == ' ') menuSelect();
        if (!(key >= '1' && key <= '3')) return;
    }

    if (gameState == STATE_PLAYING) {
        if (key == 'e') switchDimension();
        if (key == 'q') spawnGhost();
        if (key == 'f') tryOpenPuzzle();
        if (key == 'r') respawn();
    }
    if (key >= '1' && key <= '3') {
        if (key == '1') { resetTotals(); }
        loadLevel(key - '1');
    }
    if (key == 13 && gameState == STATE_LEVEL_DONE) loadLevel(curLevel + 1);
    if (key == 13 && gameState == STATE_VICTORY) {
        resetTotals();
        loadLevel(0);
    }
    if (key == 13 && gameState == STATE_GRADUATED) {
        resetTotals();
        loadLevel(0);
    }
    if (key == '+' || key == '=') camDist -= 1.0f;
    if (key == '-' || key == '_') camDist += 1.0f;
    if (camDist < 5.0f)  camDist = 5.0f;
    if (camDist > 30.0f) camDist = 30.0f;
}

static void keyUp(unsigned char key, int, int) {
    if (key >= 'A' && key <= 'Z') key = (unsigned char)(key + 32);
    keys[key] = false;
}

static void special(int key, int, int) {
    if (gameState == STATE_MENU || gameState == STATE_PAUSED) {
        if (key == GLUT_KEY_UP)   menuMove(-1);
        if (key == GLUT_KEY_DOWN) menuMove(1);
        if (gameState == STATE_MENU && menuIndex == 1 &&
            (key == GLUT_KEY_LEFT || key == GLUT_KEY_RIGHT)) {
            selectedLevel = (selectedLevel + (key == GLUT_KEY_RIGHT ? 1 : NUM_LEVELS - 1)) % NUM_LEVELS;
            loadLevel(selectedLevel);          // preview the chosen level behind the menu
            gameState = STATE_MENU;
        }
        return;
    }
    if (gameState == STATE_HOWTO) { gameState = STATE_MENU; return; }
    if (gameState == STATE_PUZZLE) return;
    switch (key) {
        case GLUT_KEY_LEFT:  camYaw   -= 5.0f; break;
        case GLUT_KEY_RIGHT: camYaw   += 5.0f; break;
        case GLUT_KEY_UP:    camPitch += 3.0f; break;
        case GLUT_KEY_DOWN:  camPitch -= 3.0f; break;
    }
    if (camPitch > 75.0f) camPitch = 75.0f;
    if (camPitch < 5.0f)  camPitch = 5.0f;
}

static void mouse(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON) {
        dragging = (state == GLUT_DOWN) && gameState == STATE_PLAYING;
        lastMouseX = x; lastMouseY = y;
    }
}

static void motion(int x, int y) {
    if (!dragging) return;
    camYaw   -= (x - lastMouseX) * 0.4f;
    camPitch += (y - lastMouseY) * 0.4f;
    if (camPitch > 75.0f) camPitch = 75.0f;
    if (camPitch < 5.0f)  camPitch = 5.0f;
    lastMouseX = x; lastMouseY = y;
}

// ---------------------------------------------------------------- init / main
static void initGL() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);

    GLfloat spec[] = { 0.4f, 0.4f, 0.5f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 40.0f);

    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 40.0f);
    glFogf(GL_FOG_END, 110.0f);
}

int main(int argc, char** argv) {
    srand((unsigned)time(0));           // different puzzles every run
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(winW, winH);
    glutCreateWindow("ECHO SHIFT: The Parallel Dimension");

    initGL();
    buildStars();
    loadLevel(0);
    gameState = STATE_MENU;      // start at the main menu

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);
    glutSpecialFunc(special);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    glutTimerFunc(16, timer, 0);

    glutMainLoop();
    return 0;
}
