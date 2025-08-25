#ifdef WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <assert.h>
#ifndef WIN32
#include <libgen.h>
#include <sys/io.h>
#else
#include <io.h>
#endif
#include <fcntl.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef WIN32
#define stricmp strcasecmp
#endif

typedef struct
{
  uint8_t ex;
  uint8_t s2;
  uint8_t rc;
  uint8_t bl[16];    // Blocks stored in each extent (dir. entry)
  uint8_t blused;
} Extent;

typedef struct
{
  uint8_t fname[12];
  uint8_t user;
  uint32_t nbytes;
  Extent extents[256];   // 256 is a good top for number of dir. entries a file can use
  uint16_t extused;
} DiskDirEntry;

typedef struct
{
  int ntracks;
  int nsides;
  int nsectors;
  int sectorsize;
  int blocksize;
  int nmaxdirentries;
  int disksize;
  int reservedbytes;
  int ignorefirstentry;
  int firstsectorid;
} DiskInfo;

int  process_dsk    (FILE *fdisk, uint8_t dib[], uint8_t **bufdisk, DiskInfo *di);
int  process_edsk   (FILE *fdisk, uint8_t dib[], uint8_t **bufdisk, DiskInfo *di);
int  process_image  (uint8_t *disk, DiskInfo *di, int argc, char *argv[]);
void StringUpper    (char *s);
void ASCIItoCPM     (char *, uint8_t *);
void CPMtoASCII     (uint8_t *, char *);
int  IsSameFile     (uint8_t *a, uint8_t *b);
int  list_directory (uint8_t *disk, DiskInfo *di, DiskDirEntry *entries, int nentries);
int  get_all_files  (uint8_t *disk, DiskInfo *di, DiskDirEntry *entries, int nentries);
int  get_file       (uint8_t *disk, DiskInfo *di, DiskDirEntry *entries, int nentries, char *filename);

int main (int argc, char *argv[])
{
  FILE *fdisk;
  uint8_t *disk = NULL;
  int ret;
  DiskInfo di;
  
  if (argc < 2)
  {
    fprintf (stderr, "3edisk 0.5\n"
    "(c)2025 Miguel Angel Rodriguez Jodar (mcleod_ideafix)\n"
    "ZX Projects\n\n"
    "Usage: 3edsk dsk_file.dsk command\n\n"
    "Where command is:\n"
    "\tdir      : lists contents of the DSK image\n"
    "\tget file : get a file from the DSK image\n"
    "\tgetall   : get all files from the DSK image to the current directory\n");
    exit(1);
  }
  
  fdisk = fopen (argv[1], "rb");
  if (!fdisk)
  {
    fprintf (stderr, "Unable to open [%s]\n", argv[0]);
    exit(1);
  }
  
  uint8_t dib[256];
  fread (dib, 1, 256, fdisk);
  if (memcmp (dib, "EXTENDED CPC DSK", 16) == 0)
    ret = process_edsk(fdisk, dib, &disk, &di);
  else if (memcmp (dib, "MV - CPCEMU", 11) == 0)
    ret = process_dsk(fdisk, dib, &disk, &di);
  else
    ret = fprintf (stderr, "Unable to identify disk image type. Must be CPC DSK type (normal or extended)\n");
  fclose (fdisk);

  if (ret == 1)
  {
    //fdisk = fopen ("salida.bin", "wb");
    //fwrite (disk, 1, di.disksize, fdisk);
    //fclose (fdisk);
    process_image (disk, &di, argc, argv);
  }
  
  return 0;
}

void ASCIItoCPM (char *a, uint8_t *b)
{
  int i,j;
  
  for (i=0,j=0; i<strlen(a); i++)
  {
    if (isalpha(a[i]))
      b[j++] = toupper(a[i]);
    else if (a[i] == '.')
      for (;j<8;j++) b[j] = ' ';
    else
      b[j++] = a[i];
  }
  for (;j<11;j++) b[j] = ' ';
  b[11] = '\0';
}

void CPMtoASCII (uint8_t *a, char *b)
{
  int i,j;
  
  for (i=0,j=0; i<8; i++)
    if (a[i] != ' ')
      b[j++] = a[i];
  if (memcmp (a+8, "   ", 3) == 0)
  {
    b[j] = '\0';
  }
  else
  {    
    b[j++] = '.';
    for (; i<11; i++)
      if (a[i] != ' ')
        b[j++] = a[i];
    b[j] = '\0';
  }
}

void StringUpper (char *s)
{
  while (*s)
  {
    if (isalpha(*s))
      *s = toupper(*s);
    s++;
  }
}

int IsSameFile (uint8_t *a, uint8_t *b)
{
  int i;
  
  for (i=0; i<11 && (a[i]&0x7F) == (b[i]&0x7F); i++);
  return (i == 11);
}

int process_dsk (FILE *fdisk, uint8_t dib[], uint8_t **bufdisk, DiskInfo *di)
{
  int ntracks = dib[0x30];
  int nsides = dib[0x31];
  int tracksize = dib[0x33]*256+dib[0x32];
  size_t location = 0x100;
  uint8_t *buftrack = NULL;
  int tambufdisk = 0;
  size_t indexbufdisk = 0;
  
  *bufdisk = NULL;  
  di->ntracks = ntracks;
  di->nsides = nsides;
  
  for (int track=0; track<ntracks; track++)
  {
    for (int side=0; side<nsides; side++)
    {
      int nlogicaltrack = track*nsides + side;
      if (nlogicaltrack > 0)
        location += tracksize;
      
      buftrack = malloc (tracksize * sizeof *buftrack);
      if (!buftrack)
      {
        fprintf (stderr, "Unable to allocate %d of bytes for logical track %d\n", tracksize, nlogicaltrack);
        return 0;
      }
      
      fseek (fdisk, location, SEEK_SET);  // go to that track on disk
      int bread = fread (buftrack, 1, tracksize, fdisk);
      if (bread < tracksize)
      {
        fprintf (stderr, "Unable to read track %d:%d\n", track, side);
        return 0;
      }
      
      //fprintf (stderr, "Reading track %d:%d\n", buftrack[0x10], buftrack[0x11]);
      
      int nsectors = buftrack[0x15];
      int sizesector = 128*(1<<buftrack[0x14]);
      
      di->nsectors = nsectors;   // assume all tracks have the same number of sectors...
      tambufdisk += nsectors * sizesector;
      *bufdisk = realloc (*bufdisk, tambufdisk);
      if (!*bufdisk)
      {
        fprintf (stderr, "Unable to allocate %d bytes for disk buffer\n", tambufdisk);
        free (buftrack);
        return 0;
      }
      
      for (int sector = 0; sector<nsectors; sector++)
      {
        int is;
        for (is=0; is<nsectors; is++)
          if ((buftrack[0x18+is*8+0x2] & 0x3F) == sector+1)   // masqueamos sectores tipo C1,C2... o 41,42... en +3/PCW
            break;
        if (is == nsectors)
        {
          fprintf (stderr, "Sector %d:%d:%d not found (missing mark ID on sector)\n", track, side, sector+1);
          free (*bufdisk);
          free (buftrack);
          return 0;
        }
        memcpy (*bufdisk+indexbufdisk, buftrack+0x100+sizesector*is, sizesector);
        indexbufdisk += sizesector;        
        if (track == 0 && side == 0 && sector == 0)
          di->firstsectorid = buftrack[0x18+is*8+0x2];  // guardamos ID del primer sector en pista 0, cara 0, para identificar tipo de disco
      }
      
      free (buftrack);
    }
  }
  di->disksize = tambufdisk;
  return 1;
}

int process_edsk (FILE *fdisk, uint8_t dib[], uint8_t **bufdisk, DiskInfo *di)
{
  int ntracks = dib[0x30];
  int nsides = dib[0x31];
  uint8_t *tst = dib+0x34;
  size_t location = 0x100;
  uint8_t *buftrack = NULL;
  int tambufdisk = 0;
  size_t indexbufdisk = 0;
  
  *bufdisk = NULL;  
  di->ntracks = ntracks;
  di->nsides = nsides;
  
  for (int track=0; track<ntracks; track++)
  {
    for (int side=0; side<nsides; side++)
    {
      int nlogicaltrack = track*nsides + side;
      if (nlogicaltrack > 0)
        location += tst[nlogicaltrack-1]*256;
      if (tst[nlogicaltrack] == 0)
        continue;   // a track size of 0 means no track info block for this track
      
      buftrack = malloc (tst[nlogicaltrack]*256 * sizeof *buftrack);
      if (!buftrack)
      {
        fprintf (stderr, "Unable to allocate %d of bytes for logical track %d\n", tst[nlogicaltrack]*256, nlogicaltrack);
        return 0;
      }
      
      fseek (fdisk, location, SEEK_SET);  // go to that track on disk
      int bread = fread (buftrack, 1, tst[nlogicaltrack]*256, fdisk);
      if (bread < tst[nlogicaltrack]*256)
      {
        fprintf (stderr, "Unable to read track %d:%d\n", track, side);
        return 0;
      }
      
      //fprintf (stderr, "Reading track %d:%d\n", buftrack[0x10], buftrack[0x11]);
      
      int nsectors = buftrack[0x15];
      int sizesector = 128*(1<<buftrack[0x14]);
      
      di->nsectors = nsectors;   // assume all tracks have the same number of sectors...
      tambufdisk += nsectors * sizesector;
      *bufdisk = realloc (*bufdisk, tambufdisk);
      if (!*bufdisk)
      {
        fprintf (stderr, "Unable to allocate %d bytes for disk buffer\n", tambufdisk);
        free (buftrack);
        return 0;
      }
      
      for (int sector = 0; sector<nsectors; sector++)
      {
        int is;
        for (is=0; is<nsectors; is++)
          if ((buftrack[0x18+is*8+0x2] & 0x3F) == sector+1)   // masqueamos sectores tipo C1,C2... o 41,42... en +3/PCW
            break;
        if (is == nsectors)
        {
          fprintf (stderr, "Sector %d:%d:%d not found (missing mark ID on sector)\n", track, side, sector+1);
          free (*bufdisk);
          free (buftrack);
          return 0;
        }
        memcpy (*bufdisk+indexbufdisk, buftrack+0x100+sizesector*is, sizesector);
        indexbufdisk += sizesector;        
        if (track == 0 && side == 0 && sector == 0)
          di->firstsectorid = buftrack[0x18+is*8+0x2];  // guardamos ID del primer sector en pista 0, cara 0, para identificar tipo de disco
      }
      
      free (buftrack);
    }
  }
  di->disksize = tambufdisk;
  return 1;
}

int process_image (uint8_t *disk, DiskInfo *di, int argc, char *argv[])
{
  DiskDirEntry *entries = NULL;
  int nentries = 0;
  int i, j, b;
  
  if (disk[0] == 0 && disk[1] == di->nsides-1 && disk[2] == di->ntracks && disk[3] == di->nsectors)    
  {
    //byte0: formato (0=PCW/+3)
    //byte1: sidedness
    //byte2: pistas/side
    //byte3: sectores/pista
    //byte4: psh = log2(tam_sector) - 7   (2 ? 512 B)
    //byte5: off = nº de pistas reservadas (system tracks)
    //byte6: bsh (? BLS = 128·2^bsh)
    //byte7: nº de bloques de directorio
    //byte8: GAP r/w
    //byte9: GAP format
    //bytes10–14: 0
    //byte15: checksum “fiddle” (boot)
      
    di->blocksize = 128*(1<<disk[6]);
    di->nmaxdirentries = disk[7] * di->blocksize / 32;
    di->sectorsize = 128*(1<<disk[4]);
    di->reservedbytes = disk[5] * di->nsectors * di->sectorsize;    
    if (di->reservedbytes == 0)
      di->ignorefirstentry = 1;
    else
      di->ignorefirstentry = 0;
  }
  else
  {
    if ((di->firstsectorid & 0xC0) == 0x40)
    {
      di->blocksize = 1024;
      di->nmaxdirentries = 64;
      di->sectorsize = 512;
      di->reservedbytes = 2 * di->nsectors * di->sectorsize;
      di->ignorefirstentry = 0;
    }
    else if ((di->firstsectorid & 0xC0) == 0xC0)
    {
      di->blocksize = 1024;
      di->nmaxdirentries = 64;
      di->sectorsize = 512;
      di->reservedbytes = 0;
      di->ignorefirstentry = 0;
    }
    else
    {
      di->blocksize = 1024;
      di->nmaxdirentries = 64;
      di->sectorsize = 512;
      di->reservedbytes = 1 * di->nsides * di->nsectors * di->sectorsize;
      di->ignorefirstentry = 0;
    }    
  }
  
  // List of files will be in here
  entries = NULL;
  nentries = 0;
  int entrybeenprocessed = 1;
  
  uint8_t *entry = disk + di->reservedbytes;
  
  if (di->ignorefirstentry)  // mark first directory entry as deleted if first entry should be ignored
    entry[0] = 0xE5;

  int exm = di->blocksize/1024-1;
  
  while (entrybeenprocessed)  // while there is still dir.entries to process
  {
    entry = disk + di->reservedbytes - 32;  // point 32 bytes before the first entry
    for (i=0; i<di->nmaxdirentries; i++)    // for each dir.entry, check if it's already in the list, or is a new file to the list
    {
      entry += 32;  // next dir.entry (put here so continue will reach it)
      entrybeenprocessed = 0;
      if (entry[0] == 0xE5)
        continue;
      
      entrybeenprocessed = 1;  // if we reach here, there is an entry to process
      for (j=0; j<nentries; j++)
        if (IsSameFile (entry+1, entries[j].fname))
          break;
      if (j == nentries)  // Fichero nuevo
      {
        nentries++;
        entries = realloc (entries, nentries * sizeof *entries);
        memcpy (entries[j].fname, entry+1, 11);
        entries[j].fname[11] = '\0';
        entries[j].user = entry[0];
        entries[j].extused = 0;
        entries[j].nbytes = 0;        
      }
      entry[0] = 0xE5;  // borramos entrada ya procesada
      int iext = entry[14]*32 + (entry[12] & 0x1F);  // para ponerlas ya en orden.   //entries[j].extused;
      if (iext >= entries[j].extused)
        entries[j].extused = iext + 1;
      entries[j].extents[iext].ex = entry[12];
      entries[j].extents[iext].s2 = entry[14];
      entries[j].extents[iext].rc = entry[15];
      entries[j].extents[iext].blused = 0;
      for (b=0; b<16 && entry[16+b] != 0; b++)
      {
        entries[j].extents[iext].bl[b] = entry[16+b];
        entries[j].extents[iext].blused++;
      }  
      int regsinentry = (entries[j].extents[iext].rc & 0x7F) + 128 * ( (entries[j].extents[iext].ex & exm) + (entries[j].extents[iext].rc >> 7) );
      entries[j].nbytes += regsinentry * 128;      
    }  
  }

  if (argc >= 3 && stricmp (argv[2], "dir") == 0)
  {
    int ret = list_directory (disk, di, entries, nentries);
    free (entries);
    return ret;
  }
  
  if (argc >= 4 && stricmp (argv[2], "get") == 0)
  {
    int ret = get_file (disk, di, entries, nentries, argv[3]);
    free (entries);
    return ret;
  }

  if (argc >= 3 && stricmp (argv[2], "getall") == 0)
  {
    int ret = get_all_files (disk, di, entries, nentries);
    free (entries);
    return ret;
  }
  
  return 0;
}

int list_directory (uint8_t *disk, DiskInfo *di, DiskDirEntry *entries, int nentries)
{
  for (int i=0; i<nentries; i++)
  {
    char fname[13];
    
    CPMtoASCII (entries[i].fname, fname);
    printf ("%-12.12s", fname);
    printf ("   %7d bytes\n", entries[i].nbytes);
  }  
  return 1;  
}

int get_all_files (uint8_t *disk, DiskInfo *di, DiskDirEntry *entries, int nentries)
{
  int ret = 1;
  
  for (int i=0; i<nentries; i++)
  {
    char fname[13];
    
    CPMtoASCII (entries[i].fname, fname);
    ret = get_file (disk, di, entries, nentries, fname);
  }  
  return ret;
}

int get_file (uint8_t *disk, DiskInfo *di, DiskDirEntry *entries, int nentries, char *filename)
{
  uint8_t fnamecm[12];  // file name from command line in 8+3 format
  int nbytes = 0;
  int i,j,b;

  ASCIItoCPM (filename, fnamecm);
  StringUpper (filename);
  
  for (i=0; i<nentries; i++)
  {
    if (IsSameFile (fnamecm, entries[i].fname))
    {
      FILE *fout;
      
      fout = fopen (filename, "wb");
      if (!fout)
      {
        fprintf (stderr, "File [%s] couldn't be created.\n", filename);
        return 0;
      }
      nbytes = entries[i].nbytes;
      for (j=0; j<entries[i].extused; j++)
      {
        for (b=0; b<entries[i].extents[j].blused; b++)
        {
          if (nbytes >= di->blocksize)
          {
            fwrite (disk + di->reservedbytes + entries[i].extents[j].bl[b] * di->blocksize, 1, di->blocksize, fout);
            nbytes -= di->blocksize;
          }
          else
          {
            fwrite (disk + di->reservedbytes + entries[i].extents[j].bl[b] * di->blocksize, 1, nbytes, fout);              
          }            
        }
      }
      fclose (fout);
      return 1;
    }
  }
  fprintf (stderr, "File [%s] not found\n", filename);
  return 0;
}
