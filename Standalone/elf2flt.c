#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>

typedef uint32_t uaddr_t;
#include "../Kernel/include/elf.h"
#include "../Kernel/include/flat.h"

#define perror_exit(msg) \
	do { perror(msg); exit(1); } while (0)
#define alignup(v,a) \
	(uint32_t)((intptr_t)((v) + (a)-1) & ~((a)-1))

#define ELFSTRUCT(offset) \
	(void*)((uint8_t*)elffile + offset)

static uint8_t *map_start, *map_end;
static int crossendian = 0;
static int stacksize = 4096;
static uint16_t arch;
static uint8_t *memory;
static unsigned int memory_size;
static unsigned int verbose;

#define reverse16(x)	((((x) & 0xFF) << 8) | (((x) & 0xFF00) >> 8))
#define reverse32(x)	((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | \
                         (((x) & 0xFF0000) >> 8) | (((x >> 24) & 0xFF)))

static uint32_t endian32(uint32_t x)
{
	if (crossendian)
		x = reverse32(x);
	return x;
}

static uint16_t endian16(uint16_t x)
{
	if (crossendian)
		x = reverse16(x);
	return x;
}

static const char *secnames[19] = {
/* 0 */
	"empty",
	"progbits",
	"symtab",
	"strtab",
	"rela",
	"hash",
	"dynamic",
	"note",
	"nobits",
	"rel",
/* 10 */
	"shlib",
	"dynsym",
	"???",
	"???",
	"init_array",
	"fini_array",
	"preinit_array",
	"group",
	"symtab_shndx"
};

static const char *sectype(int i)
{
	if (i <= 19)
		return secnames[i];
	return "???";
}

static void valid(void *p, size_t size)
{
	if (((uint8_t *)p) + size > map_end) {
		fprintf(stderr, "elf2flt: input appears corrupt.\n");
		exit(1);
	}
}

static void syntax_error(void)
{
	fprintf(stderr, "syntax: elf2flt [-s stacksize] -o outputfile inputfile\n");
	exit(1);
}

static void write_error(void)
{
	perror_exit("cannot write output file");
}

void* load_file(const char* filename)
{
	uint8_t *ptr;
	int fd = open(filename, O_RDONLY);
	if (fd == -1)
		perror_exit("cannot open input file");
	
	struct stat st;
	if (fstat(fd, &st) == -1)
		perror_exit("cannot stat input file");

	if (st.st_size < 128) {
		fprintf(stderr, "elf2flt: '%s' is too short.\n", filename);
		exit(1);
	}
	ptr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (ptr == MAP_FAILED)
		perror_exit("cannot map input file");

	map_start = ptr;
	map_end = ptr + st.st_size;
	return ptr;
}

#define MAX_RELOCS	32768

static uint32_t relocs[MAX_RELOCS];
static unsigned int next_reloc = 0;

static void add_reloc(unsigned int reloc)
{
	if (next_reloc == MAX_RELOCS) {
		fprintf(stderr, "elf2flt: too many locations - %d.\n",
			next_reloc);
		exit(1);
	}
	relocs[next_reloc++] = reloc;
}

static void add32(uint32_t offset, uint32_t val)
{
	uint32_t v;
	if (offset > memory_size - 3) {
		fprintf(stderr, "elf2flt: out of range relocation %d (max is %d)\n",
			offset, memory_size - 3);
		exit(1);
	}
	/* Add the offset to the file endian possibly unaligned 32bit
	   value at offset in the virtual memory image */
	memcpy(&v, memory + offset, sizeof(uint32_t));
	v = endian32(v) + offset;
	v = endian32(v);
	memcpy(memory + offset, &v, sizeof(uint32_t));
}

/* Without addend - just add to the relocation table */
static void relocate_rel_arm(Elf32_Rel *rel, int relcount)
{
	for (int i=0; i<relcount; i++)
	{
		uint32_t type = ELF32_R_TYPE(endian32(rel->r_info));
		uint32_t addr;
		switch (type)
		{
			case R_ARM_RELATIVE:
				addr = endian32(rel->r_offset);
				break;

			default:
				fprintf(stderr, "Unknown ELF relocation type %d\n", type);
				exit(1);
		}

		addr = htonl(addr);
		add_reloc(addr);
		rel++;
	}
}

/* With addend */
static void relocate_rela_arm(Elf32_Rela *rel, unsigned int relcount)
{
	unsigned int i;
	for (i = 0; i < relcount; i++)
	{
		uint32_t type = ELF32_R_TYPE(endian32(rel->r_info));
		uint32_t addr;
		switch (type)
		{
			default:
				fprintf(stderr, "Unknown ELF relocation type %d\n", type);
				exit(1);
		}

		addr = htonl(addr);
		add_reloc(addr);
		rel++;
	}
}

/* 68K */
static void relocate_rel_68k(Elf32_Rel *rel, int relcount)
{
	for (int i=0; i<relcount; i++)
	{
		uint32_t type = ELF32_R_TYPE(endian32(rel->r_info));
		uint32_t addr;
		switch (type)
		{
			default:
				fprintf(stderr, "Unknown ELF relocation type %d\n", type);
				exit(1);
		}

		addr = htonl(addr);
		add_reloc(addr);
		rel++;
	}
}

/* With addend */
static void relocate_rela_68k(Elf32_Rela *rel, unsigned int relcount)
{
	unsigned int i;
	for (i = 0; i < relcount; i++)
	{
		uint32_t type = ELF32_R_TYPE(endian32(rel->r_info));
		uint32_t addr;
		switch (type)
		{
			/* Adjust the memory image as well to cope with the addend */
			case R_68K_32:
				add32(endian32(rel->r_offset), endian32(rel->r_addend));
				break;
			/* Already fixed up by the linker */
			case R_68K_PC32:
			case R_68K_PC16:
				continue;

			default:
				fprintf(stderr, "Unknown ELF relocation type %d\n", type);
				exit(1);
		}
		addr = htonl(addr);
		add_reloc(addr);
		rel++;
	}
}

void relocate_rel(Elf32_Rel *rel, int relcount)
{
	switch(arch) {
	case EM_68K:
		relocate_rel_68k(rel, relcount);
		break;
	case EM_ARM:
		relocate_rel_arm(rel, relcount);
		break;
	}
}

void relocate_rela(Elf32_Rela *rel, int relcount)
{
	switch(arch) {
	case EM_68K:
		relocate_rela_68k(rel, relcount);
		break;
	case EM_ARM:
		relocate_rela_arm(rel, relcount);
		break;
	}
}


#define MAX_RELOC_SETS	32
static struct {
	unsigned int offset;
	unsigned int count;
	unsigned int addend;
} reloctab[MAX_RELOC_SETS];

static unsigned int num_reloc_sects = 0;
static unsigned int num_relocs = 0;


static void add_reloc32(unsigned int offset, unsigned int count)
{
	if (num_reloc_sects == MAX_RELOC_SETS) {
		fprintf(stderr, "elf2flt: too many relocation sections.\n");
		exit(1);
	}
	reloctab[num_reloc_sects].offset = offset;
	reloctab[num_reloc_sects].count = count;
	reloctab[num_reloc_sects++].addend = 0;
	num_relocs += count;
}

static void add_reloca32(unsigned int offset, unsigned int count)
{
	if (num_reloc_sects == MAX_RELOC_SETS) {
		fprintf(stderr, "elf2flt: too many relocation sections.\n");
		exit(1);
	}
	reloctab[num_reloc_sects].offset = offset;
	reloctab[num_reloc_sects].count = count;
	reloctab[num_reloc_sects++].addend = 1;
	num_relocs += count;
}

int main(int argc, char* const* argv)
{
	const char* inputfilename = NULL;
	const char* outputfilename = NULL;
	FILE *outfp;
	Elf32_Shdr *shdr;
	Elf32_Ehdr *elffile;
	uint32_t bsshi = 0;
	uint32_t bsslo = 0xffffffff;
	uint32_t datahi = 0;
	uint32_t datalo = 0xffffffff;
	uint32_t texthi = 0;
	uint32_t textlo = 0xffffffff;
	uint32_t reloff = 0xffffffff;
	uint32_t relcount = 0;
	unsigned int seentext = 0;
	unsigned int seendata = 0;
	unsigned int seenbss = 0;
	unsigned int i;

	for (;;)
	{
		int opt = getopt(argc, argv, "s:o:v");
		if (opt == -1)
			break;

		switch (opt)
		{
			case 'o':
				outputfilename = optarg;
				break;

			case 's':
				stacksize = strtoul(optarg, NULL, 0);
				break;

			case 'v':
				verbose = 1;
				break;
			default:
				syntax_error();
		}
	}

	if (!outputfilename
			|| ((optind+1) != argc))
		syntax_error();
	inputfilename = argv[optind];

	elffile = load_file(inputfilename);

	if (memcmp(elffile->e_ident + 1, "ELF", 3)) {
		fprintf(stderr, "elf2flt: '%s' is not an ELF file.\n", inputfilename);
		exit(1);
	}
	if (elffile->e_ident[EI_CLASS] != ELFCLASS32) {
		fprintf(stderr, "elf2flt: only 32bit ELF is supported.\n");
		exit(1);
	}
	if (elffile->e_ident[EI_DATA] == ELFDATA2MSB)
		crossendian = 1;

	arch = endian16(elffile->e_machine);
	if (arch != EM_68K && arch != EM_ARM) {
		fprintf(stderr, "elf2flt: unsupported machine type %d.\n", arch);
		exit(1);
	}

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	crossendian ^= 1;
#endif
	if (crossendian && verbose)
		printf("[Cross endian conversion]\n");
	outfp = fopen(outputfilename, "wb");
	if (!outfp)
		perror_exit("cannot open output file");
	/* Scan the section headers looking for stuff. */

	shdr = ELFSTRUCT(endian32(elffile->e_shoff));
	for (int i=0; i<endian16(elffile->e_shnum); i++)
	{
		uint32_t seclo, sechi, flags;
		Elf32_Shdr* sh = &shdr[i];

		valid(sh, sizeof(Elf32_Shdr));

		seclo = endian32(sh->sh_addr);
		sechi = seclo + endian32(sh->sh_size);
		flags = endian32(sh->sh_flags);
		if (verbose)
			printf("Section %d (%s), type %d\n", i, sectype(endian32(sh->sh_type)), endian32(sh->sh_type));
		switch (endian32(sh->sh_type))
		{
			/* Things we explicitly ignore */
			case SHT_NULL:
			case SHT_SYMTAB:
			case SHT_STRTAB:
			case SHT_HASH:
			case SHT_NOTE:
				break;

			case SHT_PROGBITS: /* Initialised data */
				if (flags & SHF_ALLOC)
				{
					if (flags & SHF_EXECINSTR)
					{
						/* Text segment. */
						seentext = 1;
						if (seclo < textlo)
							textlo = seclo;
						if (sechi > texthi)
							texthi = sechi;
					}
					else
					{
						/* Data segment. */
						seendata = 1;
						if (seclo < datalo)
							datalo = seclo;
						if (sechi > datahi)
							datahi = sechi;
					}
				}
				break;


			case SHT_NOBITS: /* Zero-initialised data */
				if (flags & SHF_ALLOC)
				{
					/* BSS segment. */
					seenbss = 1;
					if (seclo < bsslo)
						bsslo = seclo;
					if (sechi > bsshi)
						bsshi = sechi;
				}
				break;

			case SHT_REL: /* Relocation section */
				reloff = endian32(sh->sh_offset);
				relcount = endian32(sh->sh_size) / sizeof(Elf32_Rel);
				add_reloc32(reloff, relcount);
				break;

			case SHT_RELA: /* Relocation section with addends */
				reloff = endian32(sh->sh_offset);
				relcount = endian32(sh->sh_size) / sizeof(Elf32_Rela);
				add_reloca32(reloff, relcount);
				break;

			default:
				fprintf(stderr, "elf2flt: Warning unknown segment type %d (%s)\n",
					endian32(sh->sh_type), sectype(endian32(sh->sh_type)));
				break;
		}
	}

	if (verbose) {
		if (seentext)
			printf("text:  0x%08x to 0x%08x\n", textlo, texthi);
		if (seendata)
			printf("data:  0x%08x to 0x%08x\n", datalo, datahi);
		if (seenbss)
			printf("bss:   0x%08x to 0x%08x\n", bsslo, bsshi);
		printf("reloc: %d entries\n", num_relocs);
	}

	if (!seentext || !seendata || !seenbss) {
		fprintf(stderr, "elf2fit: segments missing.\n");
		exit(1);
	}
	if ((bsslo < texthi) || (bsslo < datahi) || (datalo < texthi))
	{
		fprintf(stderr, "elf2flt: overlapping segments (ELF file is too complex?)\n");
		exit(1);
	}

	/* Now, load all the PROGBITS segments into memory. */

	memory_size = datahi - textlo;
	memory = calloc(1, memory_size);
	if (memory == NULL) {
		fprintf(stderr, "elf2flt: out of memory.\n");
		exit(1);
	}
	for (int i=0; i < endian16(elffile->e_shnum); i++)
	{
		Elf32_Shdr* sh = &shdr[i];
		uint32_t flags;

		flags = endian32(sh->sh_flags);
		switch (endian32(sh->sh_type))
		{
			case SHT_PROGBITS: /* Initialised data */
				if (flags & SHF_ALLOC)
					memcpy(memory - textlo + endian32(sh->sh_addr), ELFSTRUCT(endian32(sh->sh_offset)), endian32(sh->sh_size));
				break;
		}
	}

	/* Assemble the bFLT header. */

	struct binfmt_flat flatheader =
	{
		.magic = FLAT_FUZIX_MAGIC,
		.rev = htonl(FLAT_VERSION),
		.flags = htonl(FLAT_FLAG_RAM),
		.entry = htonl(elffile->e_entry + sizeof(flatheader)),
		.data_start = htonl(datalo + sizeof(flatheader)),
		.data_end = htonl(datahi + sizeof(flatheader)),
		.bss_end = htonl(bsshi + sizeof(flatheader)),
		.reloc_start = htonl(datahi + sizeof(flatheader)),
		.reloc_count = htonl(relcount),
		.stack_size = htonl(stacksize),
	};

	if (fwrite(&flatheader, sizeof(flatheader), 1, outfp) != 1)
		write_error();

	for (i = 0; i < num_reloc_sects; i++) {
		void *rel = ELFSTRUCT(reloctab[i].offset);
		unsigned int relcount = reloctab[i].count;
		if (reloctab[i].addend)
			relocate_rela(rel, relcount);
		else
			relocate_rel(rel, relcount);
	}

	if (fwrite(memory, datahi, 1, outfp) != 1)
		write_error();
	if (fwrite(relocs, sizeof(uint32_t), next_reloc, outfp) != next_reloc)
		write_error();

	fclose(outfp);
}
