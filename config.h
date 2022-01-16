static char *starturi = "gopher://hhvn.uk";
static char *plumber = "plumb";
static int parallelplumb = 1;
static int stimeout = 5;

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
	{'1',    "Dir ", -1 },
	{'2',    "CCSO", -1 },
	{'4',    "Bin ", -1 },
	{'5',    "Bin ", -1 },
	{'9',    "Bin ", -1 },
	{'7',    "Srch", -1 },
	{'8',    "Teln", -1 },
	{'T',    "Teln", -1 },
	{'+',    "Alt ", -1 },
	{'I',    "Img ", -1 },
	{'g',    "Img ", -1 },
	{'h',    "HTML", -1 },
	{'s',    "Snd ", -1 },
	{'d',    "Doc ", -1 },
	{'3',    "ERR ", -1 },

	/* DNE! These values are actually used:
	 * -1 = default foregrounds
	 *  0 = default colour pair */
	{'\0',   NULL,	 -1, 0 },
};
