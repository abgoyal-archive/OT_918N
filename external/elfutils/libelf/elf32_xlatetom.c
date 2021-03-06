
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
//#include <endian.h>
#include <string.h>

#include "libelfP.h"

#ifndef LIBELFBITS
# define LIBELFBITS	32
#endif


Elf_Data *
elfw2(LIBELFBITS, xlatetom) (dest, src, encode)
     Elf_Data *dest;
     const Elf_Data *src;
     unsigned int encode;
{
  /* First test whether the input data is really suitable for this
     type.  This means, whether there is an integer number of records.
     Note that for this implementation the memory and file size of the
     data types are identical.  */
#if EV_NUM != 2
  size_t recsize = __libelf_type_sizes[src->d_version - 1][ELFW(ELFCLASS,LIBELFBITS) - 1][src->d_type];
#else
  size_t recsize = __libelf_type_sizes[0][ELFW(ELFCLASS,LIBELFBITS) - 1][src->d_type];
#endif


  if (src->d_size % recsize != 0)
    {
      __libelf_seterrno (ELF_E_INVALID_DATA);
      return NULL;
    }

  /* Next see whether the converted data fits in the output buffer.  */
  if (src->d_size > dest->d_size)
    {
      __libelf_seterrno (ELF_E_DEST_SIZE);
      return NULL;
    }

  /* Test the encode parameter.  */
  if (encode != ELFDATA2LSB && encode != ELFDATA2MSB)
    {
      __libelf_seterrno (ELF_E_INVALID_ENCODING);
      return NULL;
    }

  /* Determine the translation function to use.

     At this point we make an assumption which is valid for all
     existing implementations so far: the memory and file sizes are
     the same.  This has very important consequences:
     a) The requirement that the source and destination buffer can
	overlap can easily be fulfilled.
     b) We need only one function to convert from and memory to file
	and vice versa since the function only has to copy and/or
	change the byte order.
  */
  if ((BYTE_ORDER == LITTLE_ENDIAN && encode == ELFDATA2LSB)
      || (BYTE_ORDER == BIG_ENDIAN && encode == ELFDATA2MSB))
    {
      /* We simply have to copy since the byte order is the same.  */
      if (src->d_buf != dest->d_buf)
	memmove (dest->d_buf, src->d_buf, src->d_size);
    }
  else
    {
      xfct_t fctp;

      /* Get a pointer to the transformation functions.  The `#ifdef' is
	 a small optimization since we don't anticipate another ELF
	 version and so would waste "precious" code.  */
#if EV_NUM != 2
      fctp = __elf_xfctstom[src->d_version - 1][dest->d_version - 1][ELFW(ELFCLASS, LIBELFBITS) - 1][src->d_type];
#else
      fctp = __elf_xfctstom[0][0][ELFW(ELFCLASS, LIBELFBITS) - 1][src->d_type];
#endif

      /* Do the real work.  */
      (*fctp) (dest->d_buf, src->d_buf, src->d_size, 0);
    }

  /* Now set the real destination type and length since the operation was
     successful.  */
  dest->d_type = src->d_type;
  dest->d_size = src->d_size;

  return dest;
}
INTDEF(elfw2(LIBELFBITS, xlatetom))
