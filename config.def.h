static char *plumber = "xdg-open";
static char *yanker = "xclip";
static char *normsep = "│"; /* separates link number/info and description */
static char *wrapsep = "/"; /* same as above, but for wrapped lines */
static int parallelplumb = 0;
static int stimeout = 5;
static int regexflags = REG_ICASE|REG_EXTENDED;
static int mdhilight = 0; /* attempt to hilight markdown headers */
static int autotls = 0;   /* automatically try to establish TLS connections */

static short bar_pair[2] = {-1,  0};
static short uri_pair[2] = {0,   7};
static short cmd_pair[2] = {7,   0};
static short arg_pair[2] = {-1,  0};
static short err_pair[2] = {160, 0};
static short eid_pair[2] = {4,   -1};

static Scheme scheme[] = {
	/* type, name,   fg */
	{'i',    "    ", -1 },
	{'0',    "Text", -1 },
	{'1',    "Dir ",  2 },
	{'2',    "CCSO",  3 },
	{'4',    "Bin ",  4 },
	{'5',    "Bin ",  4 },
	{'9',    "Bin ",  4 },
	{'7',    "Srch",  3 },
	{'8',    "Teln",  5 },
	{'T',    "Teln",  5 },
	{'+',    "Alt ",  5 },
	{'I',    "Img ",  6 },
	{'g',    "Img ",  6 },
	{'h',    "HTML",  5 },
	{EXTR,   "Extr",  5 },
	{'s',    "Snd ",  7 },
	{'d',    "Doc ",  8 },
	{'3',    "ERR ",  1 },

	/* Experimental markdown header hilighting.
	 * mdhilight must be set for this to show.
	 * Unlike other elements of scheme, the fore-
	 * ground is used for the text itself, not
	 * the name. */
	{MDH1,	 "    ",  6 },
	{MDH2,   "    ",  5 },
	{MDH3,   "    ",  4 },
	{MDH4,   "    ",  3 },

	/* must be last, see getscheme() */
	{DEFL,   "????",  8 },
};
