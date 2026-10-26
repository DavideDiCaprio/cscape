#ifndef SPRITES_H
#define SPRITES_H

// Large Shaded Cratered Moon (22 rows x 38 cols)
#define MOON_HEIGHT 22
#define MOON_WIDTH 38

static const char *const MOON_SPRITE[MOON_HEIGHT] = {
    "            .@@@@@@@@@@@@?%.          ",
    "         @@@@###*%@@@@@#@@@@0%.       ",
    "       @@@@#*@@@@@#@@@@@#####*@@@00   ",
    "     @@@#%#*##*%@@@#%o#%#.,%00000.    ",
    "    @@@#####*#####%@*..*0000000000_   ",
    "   (###:,#@@######@#,,########*o00o00)",
    "  (######@@@#@@@#@#,,%@########*oo000)",
    "  (#####**;#*###.,'@*@#o..o*...*.o000)",
    "  (###*;#@#*.*###?@@#*..*#......#*0@@)",
    " (@@####*,@@@@@@@##..00,..00$......00)",
    " (@@#####*,%@@@#:%#.00000000oo$..,000)",
    " (@@####@@@@@%#*#*%..o00000000o*,00o0)",
    "  .##*,.*#.*#.##%o.o00000000000ooo00) ",
    "  @),..*...#*..##0.o000000000000o,00( ",
    "   #),.....@*,..0),.o000000000000000) ",
    "    #,o0o0,,*@0,000.o000000000000000  ",
    "     @0@@@o`@#*,...o000000000000000   ",
    "      @@@@...*%,..o00000000000000@    ",
    "        #@@@o.00o@@@@@@@@@@@@@@@      ",
    "          @@@@@@@@@@@@@@@@@@@@        ",
    "            *%@@@@@@@@@@@@#*          ",
    "               %0@@@@@@@@#            "};

// Watcher on the Cliff (9 rows x 12 cols, standing in profile looking right)
#define CLIFF_BOY_FRAMES_COUNT 2
#define CLIFF_BOY_HEIGHT 9
#define CLIFF_BOY_WIDTH 12

static const char *const CLIFF_BOY_FRAMES[CLIFF_BOY_FRAMES_COUNT][CLIFF_BOY_HEIGHT] = {
    // Frame 0: Calm stance, breeze ruffling hair
    {"  .d8888b.  ",
     " .d8888P'\\  ",
     " `Y88P'  _> ",
     "   /|. .|   ",
     "  / |   |\\  ",
     " |  |___| | ",
     "  `-| | |-' ",
     "    | | |   ",
     "   /  |  \\  "},
    // Frame 1: Subtle breathing & wind gust in hair/coat
    {"  .d8888b._ ",
     " ~d8888P'\\  ",
     " `Y88P'  _> ",
     "   /|. .|   ",
     "  / |   |\\  ",
     " |  |___| \\ ",
     "  `-| | |-' ",
     "    | | |   ",
     "   /  |  \\  "}};

// Running Dog / Wolf on the Valley Plain (6 rows x 22 cols)
#define DOG_FRAME_COUNT 4
#define DOG_JUMP_FRAME_COUNT 3
#define DOG_HEIGHT 6
#define DOG_WIDTH 22
#define DOG_ANIM_CADENCE 5

static const char *const DOG_RUN_FRAMES[DOG_FRAME_COUNT][DOG_HEIGHT] = {
    // Frame 0: Full extended stride (matching reference silhouette)
    {"              /\\__    ",
     "  /\\____.---'    `-. ",
     "  \\             .___> ",
     "    \\  ,----.  /      ",
     "    / /\\ \\   \\ \\      ",
     "   /_/  \\_\\   \\_\\_.   "},
    // Frame 1: Gather / mid-stride suspension
    {"              /\\__    ",
     "   \\_____.---'    `-. ",
     "   `\\           .___> ",
     "     \\ ,----.  /      ",
     "      / /\\ \\ / /      ",
     "     /_/  \\_\\_/       "},
    // Frame 2: Front paw reach & hind push-off
    {"              /\\__    ",
     "  ._\\____.---'    `-. ",
     "   `\\           .___> ",
     "    /  ,----.   \\     ",
     "   /  /      \\  /\\    ",
     "  /__/        \\/_/\\.  "},
    // Frame 3: Crossover recovery stride
    {"              /\\__    ",
     "  /\\_____.---'    `-. ",
     "  \\             .___> ",
     "    \\  ,----.  /      ",
     "     \\ \\    / /\\      ",
     "      \\_\\  /_/  \\_\\   "}};

// Jumping Dog Animation (3 phases: Launch, Apex Flight, Landing)
static const char *const DOG_JUMP_FRAMES[DOG_JUMP_FRAME_COUNT][DOG_HEIGHT] = {
    // Jump Frame 0: Upward leap (front legs reaching skyward, hind pushing off)
    {"             /\\__     ",
     "    ____.---'    `-.  ",
     "  /'           .___>  ",
     " /  / ,----.   /___   ",
     " \\_/ /      \\____/    ",
     "   /_/                "},
    // Jump Frame 1: Apex mid-air bound (body arched, legs tucked/extended)
    {"              /\\__    ",
     "  .______.---'    `-. ",
     "  `\\            .___> ",
     "    \\  ,-----.   \\__  ",
     "   _/_/       \\____/  ",
     "  /_/                 "},
    // Jump Frame 2: Descent towards ground (front paws angled down to land)
    {"              /\\__    ",
     "  \\______.---'    `-. ",
     "   \\            .___> ",
     "    \\  ,----.   /     ",
     "    /_/      \\  \\     ",
     "              \\_\\_\\   "}};

// Scrolling Valley Scenery Props (Ruin Pillars, Scrub Bushes, Outcrops)
#define PROP_HEIGHT 5
#define PROP_TYPE_COUNT 3

static const char *const PROP_SPRITES[PROP_TYPE_COUNT][PROP_HEIGHT] = {
    // Prop 0: Ancient Ruin Monolith & Wall (from reference right-of-dog)
    {"       88888888 ",
     "       |      | ",
     "       |      | ",
     "-XXXXX-|      | ",
     "|     ||      | "},
    // Prop 1: Wild Cliffside Scrub Bush
    {"   \\  ||  /     ",
     "  \\ \\_||_/ /    ",
     "   \\(####)/     ",
     "    \\####/      ",
     "    .||||.      "},
    // Prop 2: Low Ruin Block & Desert Tuft
    {"                ",
     "         \\ | /  ",
     "  [888]  \\v|v/  ",
     "  |   |   \\|/   ",
     "  |___|   .|.   "}};

// ============================================================================
// LEVEL 2 ASSETS: "DEEP IN THE WATER" (Falling Water Drop, Splash & Lake Flora)
// ============================================================================

#define WATER_DROP_FRAME_COUNT 3
#define WATER_DROP_HEIGHT 5
#define WATER_DROP_WIDTH 9

static const char *const WATER_DROP_FRAMES[WATER_DROP_FRAME_COUNT][WATER_DROP_HEIGHT] = {
    // Frame 0: Swelling droplet detaching from cavern ceiling
    {"   .|.   ",
     "   (o)   ",
     "   `.'   ",
     "         ",
     "         "},
    // Frame 1: Classic falling teardrop shape with specular highlight
    {"    .    ",
     "   /|\\   ",
     "  ( o )  ",
     "   `~'   ",
     "         "},
    // Frame 2: High-velocity elongated drop right before surface impact
    {"    |    ",
     "   .|.   ",
     "  ( # )  ",
     "  ( o )  ",
     "   `v'   "}};

#define WATER_SPLASH_FRAME_COUNT 4
#define WATER_SPLASH_HEIGHT 5
#define WATER_SPLASH_WIDTH 19

static const char *const WATER_SPLASH_FRAMES[WATER_SPLASH_FRAME_COUNT][WATER_SPLASH_HEIGHT] = {
    // Frame 0: Initial impact crater & upward rim
    {"                   ",
     "     .       .     ",
     "    \\  .   .  /    ",
     "   ._\\_(~~~)_/_.   ",
     "  ~~~=========~~~  "},
    // Frame 1: Full crown splash with airborne droplets
    {"   .     o     .   ",
     "  o  \\  .|.  /  o  ",
     "    \\ \\ |#| / /    ",
     "  .__\\(~~~~~)/__.  ",
     " ~~~~=========~~~~ "},
    // Frame 2: Central Rayleigh jet rebounding upward & outward ring
    {"         o         ",
     "   .    (|)    .   ",
     "  .     |#|     .  ",
     " ~~~~._/~~~\\_.~~~~ ",
     "~~~~-----------~~~~"},
    // Frame 3: Settling concentric wave crest
    {"                   ",
     "                   ",
     "         .         ",
     "  ~~._.~(o)~._.~~  ",
     "~~~~===========~~~~"}};

#define WATER_FLORA_HEIGHT 5
#define WATER_FLORA_TYPE_COUNT 3

static const char *const WATER_FLORA_SPRITES[WATER_FLORA_TYPE_COUNT][WATER_FLORA_HEIGHT] = {
    // Flora 0: Swaying underwater kelp fronds
    {"   (   )  ",
     "    ) (   ",
     "   (   )  ",
     "    )|/   ",
     "   .|||.  "},
    // Flora 1: Submerged lakebed coral / rock spire
    {"    .^.   ",
     "   /###\\  ",
     "  |#####| ",
     "  |##o##| ",
     " ===###==="},
    // Flora 2: Aquatic reed cluster
    {"  \\  |  / ",
     "   \\ | /  ",
     "   .\\|/.  ",
     "    |||   ",
     "   .|||.  "}};

// Leaping fish (press SPACE/W/UP on Level 2): rising -> apex -> diving, facing right
#define FISH_FRAME_COUNT 3
#define FISH_HEIGHT 3
#define FISH_WIDTH 9

static const char *const FISH_FRAMES[FISH_FRAME_COUNT][FISH_HEIGHT] = {
    // Frame 0: Bursting up out of the water, nose up-right
    {"      _o>",
     "    _/(( ",
     "><(/     "},
    // Frame 1: Horizontal glide at the apex, dorsal & belly fins spread
    {"   _/\\_  ",
     "><((((o> ",
     "    \\/   "},
    // Frame 2: Nose-down dive back toward the lake surface
    {"><(\\_    ",
     "    \\((\\ ",
     "       v "}};

// Small surface splash for the fish launch & re-entry (bottom row = waterline)
#define FISH_SPLASH_FRAME_COUNT 3
#define FISH_SPLASH_HEIGHT 3
#define FISH_SPLASH_WIDTH 9

static const char *const FISH_SPLASH_FRAMES[FISH_SPLASH_FRAME_COUNT][FISH_SPLASH_HEIGHT] = {
    {"  . | .  ",
     "   \\|/   ",
     " ~~(~)~~ "},
    {" o  .  o ",
     "  \\ | /  ",
     "~~(~~~)~~"},
    {"         ",
     "    .    ",
     " ~.(o).~ "}};

// ============================================================================
// LEVEL 3 ASSETS: "MEDIEVAL VICE" (Knight Enforcer in Aviator Shades)
// Legend encoded in sprite characters:
//   '#' / '@' : Forged Steel Sallet Helmet Dome & Cheek Guards
//   '!'       : Helmet Central Ridge Strip
//   'O'       : Raised Steel Rivets (Dome, Brow Band, Cheek Guards, Pauldrons)
//   '*' / '$' : Glowing Golden Royal Shield Badge on Helmet Brow
//   '='       : Riveted Steel Brow Band & Aviator Bridge
//   '~' / '/' : Aviator Sunglasses Lenses (Torchlight Amber Reflection)
//   'M'       : Handlebar Captain Mustache
//   ':' / '.' : Chiseled Jaw Stubble
//   '%'       : ASCII Chainmail Gorget on Neck/Throat
//   'V' / 'W' : Upturned Armor Collar & Forged Steel Shoulder Pauldrons
// ============================================================================

#define VICE_COP_ROWS 28
#define VICE_COP_COLS 56

static const char *const VICE_COP_SPRITE[VICE_COP_ROWS] = {
    "                  ._/######!O!######\\_.                 ",
    "              ._/##########!#!##########\\_.             ",
    "            ./#############!O!#############\\.           ",
    "          ./############*$$$$$$$*############\\.         ",
    "         /#############*$$$@@@$$$*#############\\        ",
    "        /##############*$$@@@@@$$*##############\\       ",
    "       |###############*$$$@@@$$$*###############|      ",
    "       |################*$$$$$$$*################|      ",
    "      .=O====O====O======*$$$$$*======O====O====O=.     ",
    "     /====================*$$$*====================\\    ",
    "     |===_______________________________________===|    ",
    "     |##| .d8888888888b.=========.d8888888888b. |##|    ",
    "     |O#|d8~~~~~//~~~~~8b=======d8~~~~~//~~~~~8b|#O|    ",
    "     |##|88~~~~//~~~~~~88   ||  88~~~~//~~~~~~88|##|    ",
    "     |##|88~~~//~~~~~~~88  /||\\ 88~~~//~~~~~~~88|##|    ",
    "     |##|Y8b._~~~~~~~_.d8P |  | Y8b._~~~~~~~_.d8P|##|   ",
    "     |O#| `Y8888888888P'   |  |  `Y8888888888P' |#O|    ",
    "     |##|   .::::::::.    / || \\    .::::::::.  |##|    ",
    "     |##|   ::::::::::.  (__/\\__)  .::::::::::  |##|    ",
    "      \\O|   `:::::.dMMMMMMb.  .dMMMMMMb.:::::'  |O/     ",
    "       \\|     `::dMMMMMMMMMMMMWMMMMMMMMMb::'    |/      ",
    "        ._     .dMMM'  `===____==='  `MMMb.    _.       ",
    "      _/VV\\_    `::   .:::::..:::::.   ::'   _/VV\\_     ",
    "    ./VVVVVV\\.   %% .:::::::||:::::::. %%  ./VVVVVV\\.   ",
    "  ._/VVVV/\\VV\\_. %%%% `:::::||:::::' %%%% _/VV/\\VVVV\\_. ",
    " ./VVVVV/%%\\VVV\\.%%%%%%%%%..||..%%%%%%%%./VVV/%%\\VVVVV\\.",
    "/VVVVVVO/%%\\OVVVV\\%%%%%%%%%%%%%%%%%%%%%/VVVVO/%%\\OVVVVV\\",
    "|VVVVVV/%%%%\\VVVVV\\===O=====O=====O===/VVVVV/%%%%\\VVVVVV|"};

#endif // SPRITES_H
