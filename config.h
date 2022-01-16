static char *starturi = "gopher://hhvn.uk";

static short bar_pair[2] = {-1, 0};
static short uri_pair[2] = {0,  7};
static short cmd_pair[2] = {7,  0};
static short arg_pair[2] = {-1, 0};
static short err_pair[2] = {0,  88};

static Scheme scheme[] = {
	{'i',	"    ",	-1 },
	{'0',	"Text",	-1 },
	{'1',	"Dir ",	-1 },
	{'2',	"CCSO",	-1 },
	{'4',	"Bin ",	-1 },
	{'5',	"Bin ",	-1 },
	{'9',	"Bin ",	-1 },
	{'7',	"Srch",	-1 },
	{'8',	"Teln",	-1 },
	{'T',	"Teln",	-1 },
	{'+',	"Alt ",	-1 },
	{'I',	"Img ",	-1 },
	{'g',	"Img ",	-1 },
	{'h',	"HTML",	-1 },
	{'s',	"Snd ",	-1 },
	{'d',	"Doc ",	-1 },
	{'3',	"ERR ", -1 },
	{'\0',	NULL,	-1, 0 },
};
