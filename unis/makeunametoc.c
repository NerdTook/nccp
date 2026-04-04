/* Run this program as
 * ./makeunametoc tabs/UnicodeData.txt NameAliases.txt > unametoc.h
 * 
 * unametoc_dict:	string literal initialized dictionary representation
 * unametoc_tree:	space optimized radix tree
 *
 * The format of the radix tree is:
 *
 * byte 0: 	either 	0x80 + (key[0] - ' ') 	(if key_len == 1)	
 * 			or		key_len					(otherwise)
 * 			either of them OR'ed with 0x40 if it has aa codepoint
 *
 * byte 1:	LSB of offset into unametoc_dict for key (if key_len > 1)
 * byte 2: 	MSB of offset into unametoc_dict for key (if key_len > 1)
 *
 * byte 3: 	LSB of codepoint (only if has a codepoint)
 * byte 4:	middle byte of codepoint (ditto)
 * byte 5: 	MSB of codepoint (ditto), OR'ed with 0x80 if node has
 * 			children, OR'ed with 0x40 if it doesn't have siblings
 *
 * byte 6:
 * byte 7:
 * byte 8:	uleb128 encoded offset to first child relative to the
 * 			end of the uleb128 (only if node has children)
 *
 * byte 9:	0xff
 * 			only if node doesn't have a codepoint
 * 			and doesn't	have siblings
 *
 * For prefixes of Unicode NR1 and NR2 rule generated names, on a node
 * representing end of the prefix codepoint is 0xd800 + index into
 * unametoc_generated array with indexes into unametoc_pairs array of
 * code points (low, high) of the ranges terminated by single 0. 0xd800
 * is NR1 rule (Hangul syllables), rest are NR2 rules.
 *
 * 									see also 4.8.1 Unicode Name Property
 *
 * ----------------------------------------------------------------------
 *
 * [1] A radix tree is a space-optimized variant of a trie (prefix tree).
 * It compresses nodes that have only one child, merging them into a
 * single node labeled with a string segment.
 *
 * 									https://iq.opengenus.org/radix-tree/
 *
 * [2] uleb128: Unsigned Little Endian Base 128) is a variable‑length
 * encoding for unsigned integers. It’s widely used in DWARF debugging
 * information (ELF/DWARF format), WebAssembly (WAT binary encoding),
 * and Android’s DEX files.
 *
 * Each byte contributes 7 bits of the integer value. 
 * The most significant bit (MSB) of each byte is a continuation flag:
 * 		1 --> 	more bytes follow.
 * 		0 --> 	this is the last byte. 
 * The remaining 7 bits (least significant 7 bits of each byte) are
 * concatenated in little‑endian order (lowest‑order 7 bits come first)
 * */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_CODE_POINT 	0x110000
#define MAX_CODE_POINT 	0x10ffff
#define NO_VALUE 		0xdc00
#define GENERATED 		0xd800
#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

struct entry { const char *name; unsigned long codepoint; };
static struct entry *entries;
static unsigned long num_allocated, num_entries;

/* Unicode 17.0 Table 4-8 */
struct generated
{
	unsigned long  low;
	unsigned long high;
	int idx, ok;
	const char *prefix;
};

/*
 * NR1 	For Hangul syllables, the Name property value is derived by rule,
 * 		as specified in Section 3.12, Conjoining Jamo Behavior, under
 * 		“Hangul Syllable Name Generation,” by concatenating a fixed prefix
 * 		string “HANGUL SYLLABLE ” and appropriate values of the
 * 		Jamo_Short_Name property.
 *
 * For example, the name of U+D4DB is HANGUL SYLLABLE PWILH, constructed by
 * concatenation of “HANGUL SYLLABLE ” and three Jamo_Short_Name property
 * values, “P” + “WI” + “LH”.
 * */

/*
 * NR2 	For most ideographs (characters with the binary property value
 * 		Ideographic = True), the Name property value is derived by
 * 		concatenating a script-specific prefix string, as specified in
 * 		Table 4-8, to the code point, expressed in uppercase hexadecimal,
 * 		with the usual 4- to 6-digit convention.
 * */

static struct generated generated_ranges[] =
{
	{0x0AC00, 0x0D7A3, 0, 0, "HANGUL SYLLABLE "					}, /* NR1 */
	{0x03400, 0x04DBF, 1, 0, "CJK UNIFIED IDEOGRAPH-"			}, /* NR2 */
	{0x04E00, 0x09FFF, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x20000, 0x2A6DF, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x2A700, 0x2B739, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x2B740, 0x2B81D, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x2B820, 0x2CEA1, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x2CEB0, 0x2EBE0, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x2EBF0, 0x2EE5D, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x30000, 0x3134A, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x31350, 0x323AF, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x13460, 0x143FA, 2, 0, "EGYPTIAN HIEROGLYPH-"				},
	{0x17000, 0x187F7, 3, 0, "TANGUT IDEOGRAPH-"				},
	{0x18D00, 0x18D08, 3, 0, "TANGUT IDEOGRAPH-"				},
	{0x18B00, 0x18CD5, 4, 0, "KHITAN SMALL SCRIPT CHARACTER-"	},
	{0x1B170, 0x1B2FB, 5, 0, "NUSHU CHARACTER-"					},
	{0x0F900, 0x0FA6D, 6, 0, "CJK COMPATIBILITY IDEOGRAPH-"		},
	{0x0FA70, 0x0FAD9, 6, 0, "CJK COMPATIBILITY IDEOGRAPH-"		},
	{0x2F800, 0x2FA1D, 6, 0, "CJK COMPATIBILITY IDEOGRAPH-"		},
};

struct node
{
	struct node *sibling, *child;
	const char *key;
	int in_dict;
	unsigned long codepoint;
	size_t key_len, key_idx;
	size_t node_size, size_num, child_off;
};

static struct node *root, **nodes;
static unsigned long num_nodes;
static char *dict;
static unsigned char *tree;
static size_t dict_size, tree_size, max_entry_len;

static void fail (const char *s, ...)
{
	va_list ap;
	
	va_start (ap, s);
	vfprintf (stderr, s, ap);
	va_end (ap);
	fputc ('\n', stderr);

	exit (1);
}

static void * xmalloc (size_t size)
{
	void *ret = malloc (size);
  	if (ret == NULL)
  		fail ("failed to allocate %ld bytes", (long) size);
  	return ret;
}

static void * xrealloc (void *p, size_t size)
{
	void *ret = p ? realloc (p, size) : malloc (size);
	if (ret == NULL)
		fail ("failed to allocate %ld bytes", (long) size);
	return ret;
}

int main(int argc, char *argv[])
{
	if(argc != 3) fail("too few arguments to makeradixtree");

	return 0;
}
