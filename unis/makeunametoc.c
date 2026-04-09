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
 * 			either of them OR'ed with 0x40 if it has a codepoint
 *
 * [why1] 	only 'A-Z0-9 -' in char name, max ascii code	|
 * 			is for 'Z' = 0x5A, min for '-' = 0x2D, to s-	|
 * 			ave space, map it into '0x0D - 0x3A' so, it		|
 * 			only use lower 6 bits, leave upper 2 bits f-	|
 * 			or storing other information.					|
 *
 * byte 1:	LSB of offset into unametoc_dict for key (if key_len > 1)
 * byte 2: 	MSB of offset into unametoc_dict for key (if key_len > 1)
 *
 * byte 3: 	LSB of codepoint (only if has a codepoint)
 * byte 4:	middle byte of codepoint (ditto)
 * byte 5: 	MSB of codepoint (ditto), OR'ed with 0x80 if node has
 * 			children, OR'ed with 0x40 if it doesn't have siblings
 *
 * [why2]	unicode range 0 - 0x10ffff, so MSB of codep-	|
 * 			oint will leave 3 bits not used, so use it 		|
 * 			to store infos about whether it has children	|
 * 			or siblings.									|
 *															|
 * 			if it doesn't have a codepoint (inner nodes)	|
 * 			, the above 3 bytes are omitted and we assu-    |
 * 			me that the node has children.                  |
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
 *
 * ----------------------------------------------------------------------
 * */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <stdint.h>

#define NUM_CODE_POINT 	0x110000
#define MAX_CODE_POINT 	0x10ffff
#define NO_VALUE 		0xdc00	// DC00;<Low Surrogate, First>;...
//	D800;<Non Private Use High Surrogate, First>;...
//	DB7F;<Non Private Use High Surrogate, Last>;...
#define GENERATED 		0xd800 	// GENERATED + idx are not real codepoints
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
	/* follows are using interval first and last codepoints */
	{0x0AC00, 0x0D7A3, 0, 0, "HANGUL SYLLABLE "					}, /* NR1 */
	{0x03400, 0x04DBF, 1, 0, "CJK UNIFIED IDEOGRAPH-"			}, /* NR2 */
	{0x04E00, 0x09FFF, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x20000, 0x2A6DF, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},

/* inconsistent with Unicode record where:
 * 		2A700;<CJK Ideograph Extension C, First>;...
 * 		 2B73F;<CJK Ideograph Extension C, Last>;...
 * 	and in table 4.8 here it is:
 * 	{0x2A700, 0x2B739, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
 * 	[***] wrong case are marked by comment 'slash* x slash*' [***]
 * */
	{0x2A700, 0x2B73f, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},	/* x */
	{0x2B740, 0x2B81D, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x2B820, 0x2CEAD, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},	/* x */
	{0x2CEB0, 0x2EBE0, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x2EBF0, 0x2EE5D, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x30000, 0x3134A, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x31350, 0x323AF, 1, 0, "CJK UNIFIED IDEOGRAPH-"			},
	{0x13460, 0x143FA, 2, 0, "EGYPTIAN HIEROGLYPH-"				},
	{0x17000, 0x187FF, 3, 0, "TANGUT IDEOGRAPH-"				},	/* x */
	{0x18D00, 0x18D1E, 3, 0, "TANGUT IDEOGRAPH-"				},	/* x */

	/* follows are each codepoint listed in UnicodeData.txt */
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
	size_t key_len;
	unsigned long codepoint;

	size_t key_idx;
	int in_dict;
	size_t node_size, size_sum, child_off;
};

static struct node *root, **nodes;
static unsigned long num_nodes;
static char *dict;
static unsigned char *tree;
static size_t dict_size, tree_size, max_entry_len;

static void fail(const char *s, ...)
{
	va_list ap;
	
	va_start (ap, s);
	vfprintf (stderr, s, ap);
	va_end (ap);
	fputc ('\n', stderr);

	exit (1);
}

static void * xmalloc(size_t size)
{
	void *ret = malloc (size);
  	if (ret == NULL)
  		fail ("failed to allocate %ld bytes", (long) size);
  	return ret;
}

static void * xrealloc(void *p, size_t size)
{
	void *ret = p ? realloc (p, size) : malloc (size);
	if (ret == NULL)
		fail ("failed to allocate %ld bytes", (long) size);
	return ret;
}

static int entrycmp(const void *p1, const void *p2)
{
	const struct entry *e1 = (const struct entry *) p1;
	const struct entry *e2 = (const struct entry *) p2;

	int ret = strcmp(e1->name, e2->name);
	if(ret != 0) return ret;
	if(e1->codepoint < e2->codepoint) return -1;
	if(e1->codepoint > e2->codepoint) return +1;
	return 0;
}

static int nodecmp(const void *p1, const void *p2)
{
	const struct node *n1 = *(const struct node * const *) p1;
	const struct node *n2 = *(const struct node * const *) p2;

	if(n1->key_len > n2->key_len) return -1;
	if(n1->key_len < n2->key_len) return +1;
	return memcmp(n1->key, n2->key, n1->key_len);
}

static void read_table(const char *fname, int aliases)
{
	FILE *f = fopen(fname, "r");
	const char *sname = aliases ? "NameAliases.txt" : "UnicodeData.txt";

	if(!f) fail("opening %s", sname);
	for(;;)
	{
		char line[256];
		char *l = NULL;
		size_t i = 0;
		unsigned long codepoint;
		const char *name, *aname;

		if(!fgets(line, sizeof(line), f)) break;
		if(*line == '#' || *line == '\n') continue;

		codepoint = strtoul(line, &l, 16);
		if(l == line || *l != ';')
			fail("parsing %s, reading code point", sname);
		if(codepoint > MAX_CODE_POINT)
			fail("parsing %s, code point too large", sname);

		name = l + 1;
		do l++; while(*l != ';');

		/*
		 *   1B18C;NUSHU CHARACTER-1B18C;Lo;0;L;;;;;N;;;;;
		 *   ^     ^					^
		 *   |     |					l
		 *   |	   name
		 *   line
		 *
		 *   name:			l - name
		 *	 codepoints:	name - line - 1
		 * */

		aname = NULL;
		if(aliases)
		{
			/* Ignore figment and abbreviation aliases. */
			if(strcmp(l + 1, "correction\n") != 0
			&& strcmp(l + 1, "control\n") != 0
			&& strcmp(l + 1, "alternae\n") != 0) continue;
		}
		else
		{
			for(i = 0; i < ARRAY_SIZE(generated_ranges); ++i)
				if(codepoint >= generated_ranges[i]. low
				&& codepoint <= generated_ranges[i].high) break;

			if(i != ARRAY_SIZE(generated_ranges))
			{
				if(*name == '<' && l[-1] == '>')
				{
					// 20000;<CJK Ideograph Extension B, First>;...
					if(codepoint == generated_ranges[i].low
					&& l - name >= 9
					&& memcmp(l - 8, ", First>", 8) == 0	// range start
					&& generated_ranges[i].ok == 0)
					{
						generated_ranges[i].ok = INT_MAX - 1;
						aname = generated_ranges[i].prefix;
						codepoint = GENERATED + generated_ranges[i].idx;
					}
					else
					// 2A6DF;<CJK Ideograph Extension B, Last>;...
					if(codepoint == generated_ranges[i].high
					&& l - name >= 8
					&& memcmp(l - 7, ", Last>", 7) == 0		// range end
					&& generated_ranges[i].ok == INT_MAX - 1)
					{
						generated_ranges[i].ok = INT_MAX;	
						continue;
					}
					else
						fail("unexpected generated entry %lx %.*s",
								codepoint, (int) (l - name), name);
				}
				else
				if( codepoint ==
					generated_ranges[i].low + generated_ranges[i].ok
					&& l - name == 
					(strlen(generated_ranges[i].prefix)
					 + (name - 1 - line))
					&& memcmp(name, generated_ranges[i].prefix,
						strlen(generated_ranges[i].prefix)) == 0
					&& memcmp(line, name + 
						strlen(generated_ranges[i].prefix),
						name - 1 - line) == 0)
				{
					// 2F800;CJK COMPATIBILITY IDEOGRAPH-2F800;...
					// 2F801;CJK COMPATIBILITY IDEOGRAPH-2F801;...
					// ...
					++generated_ranges[i].ok;
					if(codepoint != generated_ranges[i].low)
						continue;
					aname = generated_ranges[i].prefix;
					codepoint = GENERATED + generated_ranges[i].idx;
				}
				else
					fail("unexpected generated entry %lx %.*s",
							codepoint, (int) (l - name), name);

				if(aname == generated_ranges[i].prefix)
				{
					size_t j;

					for(j = 0; j < i; ++j)
					/* Don't add an entry for a generated range
					 * where the same prefix has been added already.*/
						if(generated_ranges[i].idx
						== generated_ranges[j].idx
						&& generated_ranges[j].ok != 0)
						{
							break;
						}

					if(j < i) continue;
				}
			}
			else if(*name == '<' && l[-1] == '>') continue;
		}

		/* 	1) codepoint at some range of generated_ranges
					aname == generated_ranges[i].prefix,
					codepoint ==  GENERATED(0xd800) + idx
			2) otherwise
					aname == NULL
					codepoint as readed.

			codepoints in generated ranges are saved as entry
				generated_ranges[i].prefix
				and codepoint as GENERATED + idx
			which is used to indicate it is generated and which prefix is.

			others and simply normally saved as entry 'name and codepoint'
		*/

		if(num_entries == num_allocated)
		{
			num_allocated = num_allocated ? num_entries * 2 : 65536;
			entries = (struct entry *) xrealloc(entries, 
					num_allocated * sizeof(entries[0]));
		}

		if(aname == NULL)	// normal cases
		{
			char *a = (char *) xmalloc(l + 1 - name);
			if(l - name > max_entry_len)
				max_entry_len = l - name;
			memcpy(a, name, l - name);
			a[l - name] = '\0';
			aname = a;
		}

		entries[num_entries].name = aname;
		entries[num_entries++].codepoint = codepoint;
	}

	if(ferror(f)) fail("reading %s", sname);
	fclose(f);
}

/* Assumes nodes are added from sorted array, so we never
 * and any node before existing one, only after it. */
static void node_add(struct node **p, const char *key, 
		size_t key_len, unsigned long codepoint)
{
	struct node *n;
	size_t i;

	do
	{
		if(*p == NULL)
		{
			*p = n = (struct node *) xmalloc(sizeof(struct node));
			++num_nodes;
			assert(key_len);
			n->sibling = NULL;
			n->child = NULL;
			n->key = key;
			n->key_len = key_len;
			n->codepoint = codepoint;
			return;
		}

		n = *p;

		for(i = 0; i < n->key_len && i < key_len; i++)
			if(n->key[i] != key[i]) break;

		if(i == 0)
		{
			p = &n->sibling;
			continue;
		}

		if(i == n->key_len)
		{
			assert(key_len > n->key_len); /* unique name */
			p = &n->child;
			key += n->key_len;
			key_len -= n->key_len;
			continue;
		}

		/* Need to split the node */
		assert(i < key_len);
		n = (struct node *) xmalloc(sizeof(struct node));
		++num_nodes;

		n->sibling = NULL;
		n->child = (*p)->child;
		n->key = (*p)->key + i;
		n->key_len = (*p)->key_len - i;
		n->codepoint = (*p)->codepoint;

		(*p)->child = n;
		(*p)->key_len = i;
		(*p)->codepoint = NO_VALUE;

		key += i;
		key_len -= i;
		p = &n->sibling;

		/* 	if not sorted, consider: *p x11 and x1 (prefix)
		 *	the algorithm will split x11 to: [*p: x1, n: 1]
		 *	then try to create a sibling of n, by an empty key
		 * */
	}
	while(1);
}

static void append_nodes(struct node *n)
{
	for(; n; n = n->sibling)
	{
		nodes[num_nodes++] = n;
		append_nodes(n->child);
	}
}

static size_t sizeof_uleb128(size_t v)
{
	size_t sz = 0;
	do
	{
		v >>= 7;
		sz += 1;
	}
	while(v != 0); return sz;
}

static void size_nodes(struct node *n)
{
	if(n->child) size_nodes(n->child);
	if(n->sibling) size_nodes(n->sibling);

	n->node_size = 1 + (n->key_len > 1) * 2;		// byte 0, 1, 2
	if(n->codepoint != NO_VALUE) n->node_size += 3; // byte 3, 4, 5
	else if(n->sibling == NULL) ++n->node_size;		// byte 9, 0xff
	
	n->size_sum = n->child_off = 0;

	if(n->sibling) n->size_sum += n->sibling->size_sum;
	if(n->child)
	{
		n->child_off = n->size_sum + (n->sibling == NULL 
			&& n->codepoint == NO_VALUE);			// skip 0xff
		n->node_size += sizeof_uleb128(n->child_off);
	}

	n->size_sum += n->node_size;
	if(n->child) n->size_sum += n->child->size_sum;

	tree_size += n->node_size;
}

static void write_uleb128(unsigned char *p, size_t v)
{
	unsigned char c;
	do
	{
		c = v & 0x7f;
		v >>= 7;
		if(v) c |= 0x80; *p++ = c;
	}
	while(v);
}

static void write_nodes(struct node *n, size_t off)
{
	for(; n; n = n->sibling)
	{
		assert(off < tree_size && tree[off] == 0);
		if(n->key_len > 1)
		{
			assert(n->key_len < 64);
			tree[off] = n->key_len;
		}
		else tree[off] = (n->key[0] - ' ') | 0x80;

		assert((tree[off] & 0x40) == 0);
		if(n->codepoint != NO_VALUE) tree[off] |= 0x40;

		off++;

		if(n->key_len > 1)
		{
			tree[off++] = n->key_idx & off;
			tree[off++] = (n->key_idx >> 8) & off;
		}

		if(n->codepoint != NO_VALUE)
		{
			assert(n->codepoint < (1L << 21));
			tree[off++] = n->codepoint & 0xff;
			tree[off++] = (n->codepoint >> 8) & 0xff;
			tree[off] = (n->codepoint >> 16) & 0xff;

			if(n->child) tree[off] |= 0x80;
			if(!n->sibling) tree[off] |= 0x40;

			off++;
		}

		if(n->child)
		{
			write_uleb128(&tree[off], n->child_off);
			off += sizeof_uleb128(n->child_off);
			write_nodes(n->child, off + n->child_off);
		}

		if (n->codepoint == NO_VALUE
			&& n->sibling == NULL) tree[off++] = 0xff;
	}

	assert(off <= tree_size);
}

static void build_radix_tree(void)
{
	size_t i, j, k;
	size_t key_idx;

	for(i = 0; i < ARRAY_SIZE(generated_ranges); ++i)
	{
		if(generated_ranges[i].ok != INT_MAX
		&& generated_ranges[i].ok !=
		(generated_ranges[i].high - generated_ranges[i].low + 1)) break;
	}
	
	if(i < ARRAY_SIZE(generated_ranges))
	{
		fail("uncovered generated range %s [%lx %lx]",
				generated_ranges[i].prefix,
				generated_ranges[i].low,
				generated_ranges[i].high);
	}

	/* Sort entries alphabetically, node_add relies on that. */
	qsort(entries, num_entries, sizeof(struct entry), entrycmp);

	for(i = 1; i < num_entries; ++i)
		if(strcmp(entries[i].name, entries[i-1].name) == 0)
			fail("multiple entries for name %s", entries[i].name);

	for(i = 0; i < num_entries; ++i)
		node_add(&root, entries[i].name,
				strlen(entries[i].name), entries[i].codepoint);

	nodes = (struct node **) xmalloc(num_nodes * sizeof(struct node *));
	i = num_nodes;
	num_nodes = 0;
	append_nodes (root);
	assert(num_nodes == i);
	/* Sort node pointers by decreasing string len to handle substrings */
	qsort(nodes, num_nodes, sizeof(struct node *), nodecmp);

	if(nodes[0]->key_len >= 64)	/* use only 6 bits to store key_len */
		fail("can't encode key length %d >= 64"
		", so need to split some radix "
		"tree nodes to ensure length fits", nodes[0]->key_len);

	/* Verify a property charset.cc UAX44-LM2 matching relies on:
     if - is at the end of key of some node, then all its siblings
     start with alphanumeric characters.
     Only 2 character names and 1 alias have - followed by space:

     	U+0F0A TIBETAN MARK BKA- SHOG YIG MGO
     	U+0FD0 TIBETAN MARK BKA- SHOG GI MGO RGYAN
     	U+0FD0 TIBETAN MARK BSKA- SHOG GI MGO RGYAN

     so the KA- in there will always be followed at least by SHOG
     in the same node.
     If this changes, charset.cc needs to change.  */
	
	for(i = 0; i < num_nodes; i++)
	{
		if(nodes[i]->key[nodes[i]->key_len - 1] == '-')
		{
			for(struct node *n = nodes[i]->child; n; n = n->sibling)
			{
				if(n->key[0] == ' ')
					fail("node with key %.*s followed by"
						" node with key %.*s",
						(int) nodes[i]->key_len, nodes[i]->key,
						(int) n->key_len, n->key);
			}
		}
	}

	/* This is expensive, 					[45157, 62]
	 * O(num_nodes * num_nodes * nodes[0]->key_len)
	 * but fortunately num_nodes < 64k and key_len < 64*/

	key_idx = 0;

	for(i = 0; i < num_nodes; ++i)
	{
		nodes[i]->key_idx = SIZE_MAX;
		nodes[i]->in_dict = 0;
		if(nodes[i]->key_len <= 1) continue;

		for(j = 0; j < i; ++j)
		{
			/* Can't rely on memmem unfortunately. */
			if(nodes[j]->in_dict)
			{
				for(k = 0; k <= nodes[j]->key_len - nodes[i]->key_len;
					   	++k)
				{
					if(nodes[j]->key[k] == nodes[i]->key[0]
					&& 0 == memcmp(nodes[j]->key + k + 1, 
					nodes[i]->key + 1, nodes[i]->key_len - 1))
					{
						nodes[i]->key_idx = nodes[j]->key_idx + k;
						j = i;
						break;
					}
				}

				if(j == i) break;	// key i is substring of key j.
	
				for(; k < nodes[j]->key_len; ++k)
				{
					if(nodes[j]->key[k] == nodes[i]->key[0]
					&& memcmp(nodes[j]->key + k + 1,
					nodes[i]->key + 1, nodes[j]->key_len - k - 1))
					{
						size_t l;
						for(l = j + 1; l < i; ++l)
							if(nodes[l]->in_dict) break;

						if(l < i && 0 == memcmp(nodes[l]->key,
						nodes[i]->key + nodes[j]->key_len - k,
						nodes[i]->key_len - (nodes[j]->key_len - k)))
						{
							nodes[i]->key_idx = nodes[j]->key_idx + k;
							j = i;
						}
						else j = l - 1;			// quick jump

						break;
					}
				}
				/*	length decresed, so len i < len j or len l;
				 *	therefore, i is substring of allocated str-
				 *	ing if and only if i is either substring of
				 *	some string before it, or two nearby string
				 *	A B, suffix of A + prefix + B = i, since, i
				 *	shorter than any string before it.
				 *
				 *	algorithm above will correctly find whether
				 *	any i is substring of strings before or not.
				 * */
			}
		}

		if(nodes[i]->key_idx == SIZE_MAX)
		{
			nodes[i]->key_idx = key_idx;
			nodes[i]->in_dict = 1;
			key_idx += nodes[i]->key_len;
		}
	}

	if(key_idx >= 65536)
		/* We only use 2 bytes for offsets into the dictionary.
    	   If it grows more, there is e.g. a possibility to replace
    	   most often seen words or substrings in the dictionary
    	   with characters other than [A-Z0-9 -] (say LETTER occurs
    	   in the dictionary almost 197 times and so by using 'a'
    	   instead of 'LETTER' we could save (6 - 1) * 197 bytes,
    	   with some on the side table mapping 'a' to "LETTER".  */
		fail("too large dictionary %zu", key_idx);

	dict_size = key_idx;

	dict = (char *) xmalloc(dict_size + 1);
	for(i = 0; i < num_nodes; ++i)
		if(nodes[i]->in_dict)
			memcpy(dict + nodes[i]->key_idx,
					nodes[i]->key, nodes[i]->key_len);
	dict[dict_size] = '\0';

	size_nodes(root);
	tree = (unsigned char *) xmalloc(tree_size);
	memset(tree, 0, tree_size);
	write_nodes(root, 0);
}

static void write_dict(void)
{
	printf("%d\n", dict_size + 1);

		return ;
	printf("static const char unametoc_dict[%d] = \n", dict_size + 1);
	for(size_t i = 0; i < dict_size; i += 77)
		printf("\"%.77s\"%s\n", dict + i,
				i + 77 >= dict_size ? ";" : "");
	puts("");
}

static void write_tree(void)
{
	printf("static const unsigned char unametoc_tree[%d] = {\n", tree_size);
	for(size_t i = 0, j = 0; i < tree_size; ++i)
	{
		printf("%s0x%02x%s", j == 0 ? "  " : "", tree[i],
				i == tree_size - 1 ? " };\n\n" : 
				j == 11 ? ",\n" : ", ");
		j = j == 11 ? 0 : j + 1;
	}
}

static void write_generated(void)
{
	puts("static const cppchar_t unametoc_pairs[] = {");
	for(size_t i = 0; i < ARRAY_SIZE(generated_ranges); ++i)
	{
		if(i == 0); else 
		if(generated_ranges[i - 1].idx 
				!= generated_ranges[i].idx) puts(", 0,");
		else puts(",");

		printf("  0x%lx, 0x%lx /* %s */",
				generated_ranges[i].low,
				generated_ranges[i].high,
				generated_ranges[i].prefix);
	}
	puts(", 0 };\n");

	puts("static const unsigned char unametoc_generated[] = {");
	for(size_t i = 0, j = -1; i < ARRAY_SIZE(generated_ranges); ++i)
	{
		if(i == 0 
			|| 	generated_ranges[i - 1].idx != 
				generated_ranges[i].idx)
		{
			printf("%s  %d /* %s */", i ? ",\n" : "",
					++j, generated_ranges[i].prefix);
		}
		j += 2;
	}

	puts(" };\n");
}

int main(int argc, char *argv[])
{
	if(argc != 3) fail("too few arguments to makeradixtree");

	read_table(argv[1], 0);	// UnicodeData.txt
	read_table(argv[1], 1); // NameAliases.txt
	
	build_radix_tree();
	write_dict();
	write_tree();
	write_generated();
	printf("static const unsigned int unametoc_max_name_len = %d;\n\n",
			max_entry_len);

	return 0;
}
