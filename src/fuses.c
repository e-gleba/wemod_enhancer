#include <windows.h>

#define SENTINEL_LENGTH 32
#define FUSE_INTEGRITY 4
#define REMOVED 'r'
#define ALIGN8(p, m) ((((ULONG_PTR)(p) + 7) & ~(ULONG_PTR)7) + ((m) * 8))

#if defined(_WIN64)
#define S1 0x6E64474B70374C64ULL
#define S2 0x6262503639377A4EULL
#define S3 0x58486D4B4E57516AULL
#define S4 0x5873743942615A42ULL
#endif

typedef struct {
    char sentinel[SENTINEL_LENGTH];
    unsigned char version;
    unsigned char wire_length;
    unsigned char fuses[];
} FuseWire;

static FuseWire *find_wire(int offset) {
    char *base = (char *)GetModuleHandleA(NULL);
    if (!base) return NULL;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
#if defined(_WIN64)
    DWORD64 *start = (DWORD64 *)(ALIGN8(base, 1) + offset);
    DWORD64 *end = (DWORD64 *)(ALIGN8(base + nt->OptionalHeader.SizeOfImage - SENTINEL_LENGTH, -1) - offset);
    for (DWORD64 *p = start; p < end; ++p)
        if (p[0] == S1 && p[1] == S2 && p[2] == S3 && p[3] == S4) return (FuseWire *)p;
#endif
    return NULL;
}

BOOL disable_asar_integrity(void) {
    FuseWire *wire = find_wire(0);
    if (!wire) wire = find_wire(4);
    if (!wire || wire->version != 1 || wire->wire_length < 5) return FALSE;
    unsigned char *fuse = &wire->fuses[FUSE_INTEGRITY];
    if (*fuse == REMOVED) return TRUE;
    DWORD old;
    if (!VirtualProtect(fuse, 1, PAGE_READWRITE, &old)) return FALSE;
    *fuse = REMOVED;
    VirtualProtect(fuse, 1, old, &old);
    return TRUE;
}
