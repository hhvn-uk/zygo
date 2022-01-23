static char *plumber = "plumb";
static char *yanker = "xclip";
static int parallelplumb = 1;
static int stimeout = 5;
static int regexflags = REG_ICASE|REG_EXTENDED;
static int autotls = 1;

static short bar_pair[2] = {-1,  0};
static short uri_pair[2] = {0,   7};
static short cmd_pair[2] = {7,   0};
static short arg_pair[2] = {-1,  0};
static short err_pair[2] = {160, 0};
static short eid_pair[2] = {4,   -1};

static Scheme scheme[] = {
	/* type, name,   fg */
	{'i',    "    ", -1 },
	{'0',    "Text",  4 },
	{'1',    "Dir ",  5 },
	{'2',    "CCSO",  6 },
	{'4',    "Bin ", 10 },
	{'5',    "Bin ", 10 },
	{'9',    "Bin ", 10 },
	{'7',    "Srch",  6 },
	{'8',    "Teln",  5 },
	{'T',    "Teln",  5 },
	{'+',    "Alt ",  9 },
	{'I',    "Img ", 13 },
	{'g',    "Img ", 13 },
	{'h',    "HTML",  9 },
	{EXTR,   "Extr",  9 },
	{'s',    "Snd ", 13 },
	{'d',    "Doc ", 15 },
	{'3',    "ERR ",  8 },

	/* DNE! These values are actually used:
	 * -1 = default foregrounds
	 *  0 = default colour pair */
	{'\0',   "????",  8 },
};