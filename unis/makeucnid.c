//cc makeucnid.c -o makeucnid && ./makeucnid ucnid.tab UnicodeData.txt DerivedNormalizationProps.txt DerivedCoreProperties.txt > ucnid.h

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

enum
{
	C99			= 	1 << 0,
	CXX 		= 	1 << 1,
	N99 		= 	1 << 2,
	C11 		= 	1 << 3,
	N11 		= 	1 << 4,
	CXX23 		= 	1 << 5,
	NXX23 		= 	1 << 6,
	NOT_NFC 	= 	1 << 7,
	NOT_NFKC 	= 	1 << 8,
	MAYBE_NFC 	= 	1 << 9,
	ALL_LANG 	= 	C99 | CXX |  C11 | CXX23 | NXX23
};

#define NUM_CODE_POINT 0x110000
#define MAX_CODE_POINT 0x10ffff

static unsigned flags[NUM_CODE_POINT];
static unsigned decomp[NUM_CODE_POINT][2];
static unsigned anydecomp[NUM_CODE_POINT][2];
static unsigned char combining_value[NUM_CODE_POINT];

/*
 *	ucnid.tab:	copy from {gcc_source}/libcpp/ucnid.tab
 *
 *  Those files
 *		UnicodeData.txt
 *		DerivedNormalizationProps.txt
 *		DerivedCoreProperties.txt
 * 	are avaliable at:
 *		https://www.unicode.org/Public/UCD/latest/ucd/
 * */

static void fail(const char *s)
{
	fprintf(stderr, "fail: %s\n", s);
	exit(1);
}

/* Read /tabs/ucnid.tab and 
 * set the flagss for language versions in the header. */
static void read_ucnid(const char *fname)
{
	FILE *f = fopen(fname, "r");
	unsigned fl = 0;

	if(!f) fail("opening ucnid.tab");
	for(;;)
	{
		char line[256];
		if(!fgets(line, sizeof(line), f)) break;

			 if(strcmp(line, "[C99]\n") == 0) fl = C99;
		else if(strcmp(line, "[C99DIG]\n") == 0) fl = C99 | N99;
		else if(strcmp(line, "[CXX]\n") == 0) fl = CXX;
		else if(strcmp(line, "[C11]\n") == 0) fl = C11;
		else if(strcmp(line, "[C11NOSTART]\n") == 0) fl = C11 | N11;

		else if(isxdigit(line[0]))
		{
			char *l = line;

			while(*l)
			{
				unsigned long start, end;
				char *endptr = NULL;

		   		start = strtoul(l, &endptr, 16);
				if(endptr == l || *endptr != '-' && !isspace(*endptr))
					fail("parsing ucnid.tab [1]");

				l = endptr;
				if(*l != '-') end = start;
				else
				{
					end = strtoul(l + 1, &endptr, 16);
					if(end < start)
						fail("paring ucnid.tab, end before start");
					l = endptr;
					if(! isspace(*l))
						fail("paring ucnid.tab, junk after range");
				}

				while(isspace(*l)) l++;

				if(end > MAX_CODE_POINT)
					fail("parsing ucnid.tab, end too large");
				while(start <= end) flags[start++] |= fl;
			}
		}
	}
	if(ferror(f)) fail("reading ucnid.tab");
	fclose(f);
}

/* Read UnicodeData.txt and fill in the 'decomp' table to be the
 * decompositions of characters for which both the character
 * decomposed and all the code points in the decomposition are
 * valid for some supported language version, and the 
 * 'anydecomp' to be the decompositions of all characters. */
static void read_table(const char *fname)
{
	FILE *f = fopen(fname, "r");
	if(!f) fail("opening UnicodeData.txt");

	/*	Field 		Field Name
	 *	0			Code value							[*]
	 *	1			Character name
	 *	2			General category
	 *	3			Canonical combining classes			[*]
	 *	4			Bidirectional category
	 *	5			Character decomposition mapping		[*]
	 *	...
	 *   			In the Unicode Standard, not all of the mappings
	 *   			are full (maximal) decompositions. Recursive 
	 *   			application of look-up for decompositions will, 
	 *  			in all cases, lead to a maximal decomposition. 
	 *  ...
	 *  Field 0, 3, 5 are all we need, as marked above.
	 * */

	for(;;)
	{
		unsigned long decompto[4];
		unsigned long codepoint;
		unsigned int decompused;
		int i, j;
		char line[256], *l = NULL;

		if(!fgets(line, sizeof(line), f)) break;

		codepoint = strtoul(line, &l, 16);
		if(l == line || *l != ';')
			fail("parsing UnicodeData.txt, reading code point");
		if(codepoint > MAX_CODE_POINT)
			fail("parsing UnicodeData.txt, code point too large");

		do l++; while(*l != ';'); /* goto next semicolon */
		do l++; while(*l != ';'); /* goto next semicolon */

		/* Canonical combining class;
		 * in NFC/NFKC, they must be increasing (or zero).  */

		if(!isdigit(*++l))
			fail("parsing UnicodeData.txt, invalid combining class");
		combining_value[codepoint] = strtoul(l, &l, 10);
		decompused = flags[codepoint];
		if(*l != ';')
			fail("parsing UnicodeData.txt, junk after combining class");
		
		do l++; while(*l != ';'); /* goto next semicolon */

		if(*++l == '<') continue; /* Compatibility mapping. */

		for(i = 0; i < 4; i++)
		{
			if(*l == ';') break;
			if(!isxdigit(*l))
				fail("parsing UnicodeData.txt, decompsition format");
			decompto[i] = strtoul(l, &l, 16);
			decompused &= flags[decompto[i]];
			while(isspace(*l)) l++;
		}

		if(i > 2)
			fail("parsing UnicodeData.txt, decompositions too long");

		for(j = 0; j < i; j++)
			anydecomp[codepoint][j] = decompto[j];
		if(decompused & ALL_LANG)
			while(--i >= 0) decomp[codepoint][i] = decompto[i];
	}

	if(ferror(f)) fail("reading ucnid.tab");
	fclose(f);
}

/* Read DerivedNormalizationProps.txt and set the flags and
 * say whether a character is in NFC, NFKC, or is context-dependent. */
static void read_derived(const char *fname)
{
	FILE *f = fopen(fname, "r");
	if(!f) fail("openning DerivedNormalizationProps.txt");

	for(;;)
	{
		unsigned long start, end;
		char line[256];
		char *l = NULL;
		int not_nfc, not_nfkc, maybe_nfc;

		if(!fgets(line, sizeof(line), f)) break;

		if(line[0] ==  '#'
		|| line[0] == '\n'
		|| line[0] == '\r') continue;

		not_nfc 	= strstr(line, "; NFC_QC; N") 	!= NULL;
		not_nfkc 	= strstr(line, "; NFKC_QC; N") 	!= NULL;
		maybe_nfc 	= strstr(line, "; NFC_QC; M") 	!= NULL;

		if(not_nfc || not_nfkc || maybe_nfc)
		{
			start = strtoul(line, &l, 16);

			if(l == line)
				fail("parsing DerivedNormalizationProps.txt"
						", read start");
			if(start > MAX_CODE_POINT)
				fail("parsing DerivedNormalizationProps.txt"
						", code point too large");

			if (*l == '.' && l[1] == '.')
			{
	  			char *l2 = l + 2;
	  			end = strtoul (l + 2, &l, 16);
	  			if (l == l2 || end < start)
	    			fail ("parsing DerivedCoreProperties.txt"
							", reading code point");
	  			if (end > MAX_CODE_POINT)
					fail ("parsing DerivedCoreProperties.txt"
							", code point too large");
			}
			else end = start;

			while(start <= end)
				flags[start++] |=
					  ((not_nfc   ? NOT_NFC   : 0)
					|  (not_nfkc  ? NOT_NFKC  : 0)
					|  (maybe_nfc ? MAYBE_NFC : 0));
		}
	}

	if(ferror(f)) fail("reading DerivedNormalizationProps.txt");
	fclose(f);
}

/* Read DerivedCoreProperties.txt and fill in languages version
 * in flags from the XID_Start and XID_Continue properties. */
static void read_derivedcore(const char *fname)
{
	FILE *f = fopen(fname, "r");
	if(!f) fail("openning DerivedCoreProperties.txt");

	for(;;)
	{
		unsigned long start, end;
		int i, j;
		char line[256];
		char *l = NULL;

		if(!fgets(line, sizeof(line), f)) break;

		if(line[0] ==  '#'
		|| line[0] == '\n'
		|| line[0] == '\r') continue;

		int xids = strstr(line, "XID_Start") 	!= NULL;
		int xidc = strstr(line, "XID_Continue") != NULL;
		if(!xids && !xidc) continue;

		start = strtoul(line, &l, 16);

		if(l == line)
			fail("parsing DerivedCoreProperties.txt"
					", read start");
		if(start > MAX_CODE_POINT)
			fail("parsing DerivedCoreProperties.txt"
					", code point too large");

		if (*l == '.' && l[1] == '.')
		{
	  		char *l2 = l + 2;
	  		end = strtoul (l + 2, &l, 16);
	  		if (l == l2 || end < start)
	    		fail ("parsing DerivedCoreProperties.txt"
						", reading code point");
	  		if (end > MAX_CODE_POINT)
				fail ("parsing DerivedCoreProperties.txt"
						", code point too large");
		}
		else end = start;

		if(end < 0x80) continue;

		if(xids)
			for(;start <= end; start++)
			{
				flags[start] |=   CXX23;
				flags[start] &= ~ NXX23;
			}
		else
			for(;start <= end; start++)
				if((flags[start] & CXX23) == 0)
				{
					flags[start] |= CXX23;
					flags[start] |= NXX23;
				}
	}

	if(ferror(f)) fail("reading DerivedCoreProperties.txt");
	fclose(f);
}

/* Write out the table:
 *		unsigned short flags:	Bitmap of flags above.
 *		unsigned char combine:	Combining class of the character.
 *		unsigned int end:		Last character in the range
 *								which described by this entry.
 * */
static void write_table(void)
{
	printf("static const struct ucnrange ucnranges[] = {\n");

	unsigned last_flag = flags[0];
	int safe = decomp[0][0] == 0;
	unsigned char last_combine = combining_value[0];
	
	for(int i = 1; i <= NUM_CODE_POINT; i++)
		if(i == NUM_CODE_POINT
		|| (flags[i] != last_flag)
			&& ((flags[i] | last_flag) & ALL_LANG) // make sure character is in
		|| safe != (decomp[i][0] == 0)             // some language's character
		|| combining_value[i] != last_combine)     // set before search in table
		{
			printf( "{ %s|%s|%s|%s|%s|"
					"%s|%s|%s|%s|%s|%s"
					", %3d, %#08x },\n",
					last_flag & C99   		? "C99"   :   "  0",
					last_flag & N99   		? "N99"   :   "  0",
					last_flag & CXX   		? "CXX"   :   "  0",
					last_flag & C11   		? "C11"   :   "  0",
					last_flag & N11   		? "N11"   :   "  0",
					last_flag & CXX23 		? "CXX23" : "    0",
					last_flag & NXX23 		? "NXX23" : "    0",
					safe 					? "CID"   :   "  0",
					last_flag & NOT_NFC 	? "  0"   :   "NFC",
					last_flag & NOT_NFKC 	? "  0"   :   "NKC",
					last_flag & MAYBE_NFC 	? "CTX"   :   "  0",
					combining_value[i - 1],
					i - 1
				  );
			last_flag = flags[i];
			safe = decomp[i][0] == 0;
			last_combine = combining_value[i];
		}

	printf("};\n\n");
}

/* Return whether a given character is valid in an identifier
 * for some supported languag, either as itself or as a UCN. */
static int char_id_valid(unsigned int c)
{
	return (flags[c] & ALL_LANG)
		|| (c == 0x5f)					// '_'
		|| (c >= 0x30 && c <= 0x39)		// 0-9
		|| (c >= 0x41 && c <= 0x5a)		// A-Z
		|| (c >= 0x61 && c <= 0x7a);	// a-z
}

/* Write out the switch statement over character
 * for which it is context-dependent whether they are in NFC. */
static void write_swicth(void)
{
	printf(	"static int "
			"check_nfc(cpp_reader *pfile, cppchar_t c, cppchar_t p)\n"
			"{\n"
			"\tswitch(c)\n"
			"\t{\n");

	for(unsigned int i = 0; i < NUM_CODE_POINT; i++)
	{
		int found = 0;

		if(! (flags[i] & ALL_LANG) || ! (flags[i] & MAYBE_NFC)
		|| (i >= 0x1161 && i <= 0x1175)
		|| (i >= 0x11A8 && i <= 0x11C2)) continue;

		/* If an NFC starter character decomposes with this character I
		 * as the second character and an NFC starter character S as the
		 * first character, that latter character as a previous
		 * character means this character is not  NFC. Futhermore, any
		 * NFC starter character K maybe by a series of compositions of
		 * S with combinning characters whose combining class is greater
		 * than that of I also means this character is not NFC. */

		/* Unicode canonical composition, composing a starter s with a
		 * combining mark i always produces a starter. Therefore, the
		 * composed character j must be a starter as well – therefore
		 * it is no need to verify 'j' is starter or not.*/

		printf( "\tcase %#07x:\n"
				"\tswitch(p)\n"
				"\t{\n", i);

		for(unsigned int j = 0; j < NUM_CODE_POINT; j++)
		{
			if(anydecomp[j][1] != i) continue;
			unsigned s = anydecomp[j][0];
			if(combining_value[s] != 0 || (flags[s] & NOT_NFC) != 0) continue;

			if(char_id_valid(s))		// direct case: 	J -> S I
			{
				found = 1;
				printf("\tcase %#07x:\n", s);
			}

			// indirect case: J -> K I (K != S)
			// where k -> S Tn-1 Tn-2 ... T1 I
			// Ti combining class > that for I => I not nfc;
			for(unsigned k = 0; k < NUM_CODE_POINT; k++)
			{
				unsigned t = k;
				if(k == s || !char_id_valid(k)) continue;

				while(anydecomp[t][1] != 0
				&& combining_value[anydecomp[t][1]] > combining_value[i])
				{
					if(combining_value[t] != 0
					|| (flags[t] & NOT_NFC) != 0) break;

					t = anydecomp[t][0];
				}

				if(t == s)
				{
					found = 1;
					printf("\tcase %#07x:\n", k);
				}
			}
		}

		if(found) printf("\t\treturn 0;\n");
		else printf("\t/* Non-NFC cases not applicable to C/C++. */\n");

		printf(	"\tdefault:\n"
				"\t\treturn 1;\n"
				"\t}\n\n");
	}

	printf( "\tdefault:\n"
			"\t\tcpp_error(pfile, CPP_DL_ICE, "
					"\"Character %%x might not be NFKC\", c);\n"
			"\t\treturn 1;\n"
			"\t}\n"
			"}\n");
}

int main(int argc, char *argv[])
{
	if(argc != 5) fail ("too few arguments to makeucn");

  	read_ucnid		(argv[1]);
 	read_table		(argv[2]);
  	read_derived	(argv[3]);
  	read_derivedcore(argv[4]);

  	write_table();
	write_swicth();

	return 0;
}
