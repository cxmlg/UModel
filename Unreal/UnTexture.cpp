#define USE_NVIMAGE			1

#if !USE_NVIMAGE
#	include "libs/ddslib.h"				// texture decompression
#else
#	include <nvcore/StdStream.h>
#	include <nvimage/Image.h>
#	include <nvimage/DirectDrawSurface.h>
#	undef __FUNC__						// conflicted with our guard macros
#endif // USE_NVIMAGE


#include "Core.h"
#include "UnCore.h"
#include "UnObject.h"
#include "UnMaterial.h"
#include "UnPackage.h"

#include "UnMaterial2.h"
#include "UnMaterial3.h"


#if IPHONE
#	include <PVRTDecompress.h>
#endif

//#define PROFILE_DDS			1
//#define XPR_DEBUG			1

/*-----------------------------------------------------------------------------
	Texture decompression
-----------------------------------------------------------------------------*/

// replaces random 'alpha=0' color with black
static void PostProcessAlpha(byte *pic, int width, int height)
{
	guard(PostProcessAlpha);

	for (int pixelCount = width * height; pixelCount > 0; pixelCount--, pic += 4)
	{
		if (pic[3] != 0)	// not completely transparent
			continue;
		pic[0] = pic[1] = pic[2] = 0;
	}

	unguard;
}


static byte *DecompressTexture(const byte *Data, int width, int height, ETextureFormat SrcFormat,
	const char *Name, UPalette *Palette)
{
	guard(DecompressTexture);

	int size = width * height * 4;
	byte *dst = new byte [size];

#if 0
	{
		// visualize UV map
		memset(dst, 0xFF, size);
		byte *d = dst;
		for (int i = 0; i < width * height; i++, d += 4)
		{
			int x = i % width;
			int y = i / width;
			d[0] = d[1] = d[2] = 0;
			int x0 = x % 16;
			int y0 = y % 16;
			if (x0 == 0)			d[0] = 255;	// red - binormal axis
			else if (y0 == 0)		d[2] = 255;	// blue - tangent axis
			else if (x0 + y0 < 7)	d[1] = 128;	// gark green
		}
		return dst;
	}
#endif

	// process non-dxt formats here
	switch (SrcFormat)
	{
	case TEXF_P8:
		{
			if (!Palette)
			{
				appNotify("DecompressTexture: TEXF_P8 with NULL palette");
				memset(dst, 0xFF, size);
				return dst;
			}
			byte *d = dst;
			for (int i = 0; i < width * height; i++)
			{
				const FColor &c = Palette->Colors[Data[i]];
				*d++ = c.R;
				*d++ = c.G;
				*d++ = c.B;
				*d++ = c.A;
			}
		}
		return dst;
	case TEXF_RGB8:
		{
			const byte *s = Data;
			byte *d = dst;
			for (int i = 0; i < width * height; i++)
			{
				// BGRA -> RGBA
				*d++ = s[2];
				*d++ = s[1];
				*d++ = s[0];
				*d++ = 255;
				s += 3;
			}
		}
		return dst;
	case TEXF_RGBA8:
		{
			const byte *s = Data;
			byte *d = dst;
			for (int i = 0; i < width * height; i++)
			{
				// BGRA -> RGBA
				*d++ = s[2];
				*d++ = s[1];
				*d++ = s[0];
				*d++ = s[3];
				s += 4;
			}
		}
		return dst;
	case TEXF_L8:
		{
			const byte *s = Data;
			byte *d = dst;
			for (int i = 0; i < width * height; i++)
			{
				byte b = *s++;
				*d++ = b;
				*d++ = b;
				*d++ = b;
				*d++ = 255;
			}
		}
		return dst;
	case TEXF_CxV8U8:
		{
			const byte *s = Data;
			byte *d = dst;
			for (int i = 0; i < width * height; i++)
			{
				byte u = *s++;
				byte v = *s++;
				d[0] = u - 128;
				d[1] = v - 128;
				float uf = u / 255.0f * 2 - 1;
				float vf = v / 255.0f * 2 - 1;
				float t  = 1.0f - uf * uf - vf * vf;
				if (t >= 0)
					d[2] = 255 - 255 * appFloor(sqrt(t));
				else
					d[2] = 255;
				d[3] = 255;
				d += 4;
			}
		}
		return dst;
#if IPHONE
	case TEXF_PVRTC2:
	case TEXF_PVRTC4:
	#if PROFILE_DDS
		appResetProfiler();
	#endif
		PVRTDecompressPVRTC(Data, SrcFormat == TEXF_PVRTC2, width, height, dst);
	#if PROFILE_DDS
		appPrintProfiler();
	#endif
		return dst;
#endif // IPHONE
	}

	unsigned fourCC;

	switch (SrcFormat)
	{
	case TEXF_DXT1:
		fourCC = BYTES4('D','X','T','1');
		break;
	case TEXF_DXT3:
		fourCC = BYTES4('D','X','T','3');
		break;
	case TEXF_DXT5:
	case TEXF_DXT5N:
		fourCC = BYTES4('D','X','T','5');
		break;
	case TEXF_3DC:
		fourCC = BYTES4('A','T','I','2');
		break;
	default:
		appNotify("%s: unknown texture format %d \n", Name, SrcFormat);
		memset(dst, 0xFF, size);
		return dst;
	}

#if PROFILE_DDS
	appResetProfiler();
#endif

#if !USE_NVIMAGE

	// setup for DDSLib
	ddsBuffer_t dds;
	memcpy(dds.magic, "DDS ", 4);
	dds.pixelFormat.fourCC = fourCC;
	dds.size   = 124;
	dds.width  = width;
	dds.height = height;
	dds.data   = const_cast<byte*>(Data);
	guard(DDSDecompress);
	if (DDSDecompress(&dds, dst) != 0)
		appError("Error in DDSDecompress");
	unguard;

#else // USE_NVIMAGE

	nv::Image image;
	guard(nv::DecompressDDS);
	nv::DDSHeader header;
	header.setFourCC(fourCC & 0xFF, (fourCC >> 8) & 0xFF, (fourCC >> 16) & 0xFF, (fourCC >> 24) & 0xFF);
	header.setWidth(width);
	header.setHeight(height);
	header.setNormalFlag(SrcFormat == TEXF_DXT5N || SrcFormat == TEXF_3DC);	// flag to restore normalmap from 2 colors
	nv::MemoryInputStream input(Data, width * height * 4);	//!! size is incorrect, it is greater than should be

	nv::DirectDrawSurface dds(header, &input);
	dds.mipmap(&image, 0, 0);
	unguard;

	byte *s = (byte*)image.pixels();
	byte *d = dst;
	bool hasAlpha = image.format() == nv::Image::Format_ARGB;

	for (int i = 0; i < width * height; i++, s += 4, d += 4)
	{
		// BGRA -> RGBA
		d[0] = s[2];
		d[1] = s[1];
		d[2] = s[0];
		d[3] = s[3];
	}

#endif // USE_NVIMAGE

#if PROFILE_DDS
	appPrintProfiler();
#endif

	if (SrcFormat == TEXF_DXT1)
		PostProcessAlpha(dst, width, height);

	return dst;
	unguardf(("fmt=%s(%d)", EnumToName("ETextureFormat", SrcFormat), SrcFormat));
}


/*-----------------------------------------------------------------------------
	XBox360 texture decompression
-----------------------------------------------------------------------------*/

#if XBOX360

inline int appLog2(int n)
{
	int r;
	for (r = -1; n; n >>= 1, r++)
	{ /*empty*/ }
	return r;
}

// Input:
//		x/y		coordinate of block
//		width	width of image in blocks
//		logBpb	log2(bytesPerBlock)
// Reference:
//		XGAddress2DTiledOffset() from XDK
static unsigned GetTiledOffset(int x, int y, int width, int logBpb)
{
	assert(width <= 8192);
	assert(x < width);

	int alignedWidth = Align(width, 32);
	// top bits of coordinates
	int macro  = ((x >> 5) + (y >> 5) * (alignedWidth >> 5)) << (logBpb + 7);
	// lower bits of coordinates (result is 6-bit value)
	int micro  = ((x & 7) + ((y & 0xE) << 2)) << logBpb;
	// mix micro/macro + add few remaining x/y bits
	int offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
	// mix bits again
	return (((offset & ~0x1FF) << 3) +					// upper bits (offset bits [*-9])
			((y & 16) << 7) +							// next 1 bit
			((offset & 0x1C0) << 2) +					// next 3 bits (offset bits [8-6])
			(((((y & 8) >> 2) + (x >> 3)) & 3) << 6) +	// next 2 bits
			(offset & 0x3F)								// lower 6 bits (offset bits [5-0])
			) >> logBpb;
}

static void UntileXbox360Texture(unsigned *src, unsigned *dst, int width, int height, int blockSizeX, int blockSizeY, int bytesPerBlock)
{
	guard(UntileXbox360Texture);

	int blockWidth  = width  / blockSizeX;
	int blockHeight = height / blockSizeY;
	int logBpp      = appLog2(bytesPerBlock);
	for (int y = 0; y < blockHeight; y++)
	{
		for (int x = 0; x < blockWidth; x++)
		{
			int swzAddr = GetTiledOffset(x, y, blockWidth, logBpp);
			assert(swzAddr < blockWidth * blockHeight);
			int sy = swzAddr / blockWidth;
			int sx = swzAddr % blockWidth;
			assert(sx >= 0 && sx < blockWidth);
			assert(sy >= 0 && sy < blockHeight);
			for (int y1 = 0; y1 < blockSizeY; y1++)
			{
				for (int x1 = 0; x1 < blockSizeX; x1++)
				{
					int x2 = (x * blockSizeX) + x1;
					int y2 = (y * blockSizeY) + y1;
					int x3 = (sx * blockSizeX) + x1;
					int y3 = (sy * blockSizeY) + y1;
					assert(x2 >= 0 && x2 < width);
					assert(x3 >= 0 && x3 < width);
					assert(y2 >= 0 && y2 < height);
					assert(y3 >= 0 && y3 < height);
					dst[y2 * width + x2] = src[y3 * width + x3];
				}
			}
		}
	}
	unguard;
}


static byte *DecompressXBox360(const byte* TexData, int DataSize, ETextureFormat intFormat, int USize, int VSize,
	const char* Name, char*& Error, int Game)
{
	guard(DecompressXBox360);

	int bytesPerBlock, blockSizeX, blockSizeY, align;

	switch (intFormat)
	{
	case TEXF_DXT1:
		bytesPerBlock = 8;
		blockSizeX = blockSizeY = 4;
		align = 128;
		break;
	case TEXF_DXT3:
	case TEXF_DXT5:
	case TEXF_DXT5N:
		bytesPerBlock = 16;
		blockSizeX = blockSizeY = 4;
		align = 128;
		break;
	case TEXF_L8:
		bytesPerBlock = 1;
		blockSizeX = blockSizeY = 1;
		align = 64;
		break;
	case TEXF_RGBA8:
		bytesPerBlock = 4;
		blockSizeX = blockSizeY = 1;
		align = 32;
		break;
	default:
		{
			Error = "unknown texture format";
			return NULL;
		}
	}
	int USize1 = USize, VSize1 = VSize;
	if (align)
	{
		USize1 = Align(USize, align);
		VSize1 = Align(VSize, align);
	}

	float bpp = (float)DataSize / (USize1 * VSize1) * blockSizeX * blockSizeY;

#if BIOSHOCK
	// some verification
	float rate = bpp / bytesPerBlock;
	if (Game == GAME_Bioshock && (rate >= 1 && rate < 1.5f))	// allow placing of mipmaps into this buffer
		bpp = bytesPerBlock;
#endif // BIOSHOCK

	if (fabs(bpp - bytesPerBlock) > 0.001f)
	{
		static char errBuf[256];
		appSprintf(ARRAY_ARG(errBuf), "bytesPerBlock: got %g, need %d", bpp, bytesPerBlock);
		Error = errBuf;
		return NULL;
	}
	// decompress texture ...
	byte *pic;
	if (bytesPerBlock > 1)
	{
		// reverse byte order (16-bit integers)
		byte *buf2 = new byte[DataSize];
		const byte *p  = TexData;
		byte *p2 = buf2;
		for (int i = 0; i < DataSize; i += 2, p += 2, p2 += 2)	//?? use ReverseBytes() from UnCore (but: inplace)
		{
			p2[0] = p[1];
			p2[1] = p[0];
		}
		// decompress texture
		pic = DecompressTexture(buf2, USize1, VSize1, intFormat, Name, NULL);
		// delete reordered buffer
		delete buf2;
	}
	else
	{
		// 1 byte per block, no byte reordering
		pic = DecompressTexture(TexData, USize1, VSize1, intFormat, Name, NULL);
	}
	// untile texture
	byte *pic2 = new byte[USize1 * VSize1 * 4];
	UntileXbox360Texture((unsigned*)pic, (unsigned*)pic2, USize1, VSize1, blockSizeX, blockSizeY, bytesPerBlock);
	delete pic;
	// shrink texture buffer (remove U alignment)
	if (USize != USize1)
	{
		guard(ShrinkTexture);
		int line1 = USize  * 4;
		int line2 = USize1 * 4;
		byte *p1 = pic2;
		byte *p2 = pic2;
		for (int y = 0; y < VSize; y++)
		{
			memcpy(p2, p1, line1);
			p2 += line1;
			p1 += line2;
		}
		unguard;
	}
	return pic2;

	unguard;
}

#endif // XBOX360


/*-----------------------------------------------------------------------------
	UTexture::Decompress (UE2)
-----------------------------------------------------------------------------*/

#if UC2

struct XprEntry
{
	char				Name[64];
	int					DataOffset;
	int					DataSize;
};

struct XprInfo
{
	const CGameFileInfo *File;
	TArray<XprEntry>	Items;
	int					DataStart;
};

static TArray<XprInfo> xprFiles;

static bool ReadXprFile(const CGameFileInfo *file)
{
	guard(ReadXprFile);

	FArchive *Ar = appCreateFileReader(file);

	int Tag, FileLen, DataStart, DataCount;
	*Ar << Tag << FileLen << DataStart << DataCount;
	//?? "XPR0" - xpr variant with a single object (texture) inside
	if (Tag != BYTES4('X','P','R','1'))
	{
#if XPR_DEBUG
		printf("Unknown XPR tag in %s\n", file->RelativeName);
#endif
		delete Ar;
		return true;
	}
#if XPR_DEBUG
	printf("Scanning %s ...\n", file->RelativeName);
#endif

	XprInfo *Info = new(xprFiles) XprInfo;
	Info->File      = file;
	Info->DataStart = DataStart;
	// read filelist
	int i;
	for (i = 0; i < DataCount; i++)
	{
		int NameOffset, DataOffset;
		*Ar << NameOffset << DataOffset;
		int savePos = Ar->Tell();
		Ar->Seek(NameOffset + 12);
		// read name
		char c, buf[256];
		int n = 0;
		while (true)
		{
			*Ar << c;
			if (n < ARRAY_COUNT(buf))
				buf[n++] = c;
			if (!c) break;
		}
		buf[ARRAY_COUNT(buf)-1] = 0;			// just in case
		// create item
		XprEntry *Entry = new(Info->Items) XprEntry;
		appStrncpyz(Entry->Name, buf, ARRAY_COUNT(Entry->Name));
		Entry->DataOffset = DataOffset + 12;
		assert(Entry->DataOffset < DataStart);
		// seek back
		Ar->Seek(savePos);
		// setup size of previous item
		if (i >= 1)
		{
			XprEntry *PrevEntry = &Info->Items[i - 1];
			PrevEntry->DataSize = Entry->DataOffset - PrevEntry->DataOffset;
		}
		// setup size of the last item
		if (i == DataCount - 1)
			Entry->DataSize = DataStart - Entry->DataOffset;
	}
	// scan data
	// data block is either embedded in this block or followed after DataStart position
	for (i = 0; i < DataCount; i++)
	{
		XprEntry *Entry = &Info->Items[i];
#if XPR_DEBUG
//		printf("  %08X [%08X]  %s\n", Entry->DataOffset, Entry->DataSize, Entry->Name);
#endif
		Ar->Seek(Entry->DataOffset);
		int id;
		*Ar << id;
		switch (id)
		{
		case 0x80020001:
			// header is 4 dwords + immediately followed data
			Entry->DataOffset += 4 * 4;
			Entry->DataSize   -= 4 * 4;
			break;

		case 0x00040001:
			// header is 5 dwords + external data
			{
				int pos;
				*Ar << pos;
				Entry->DataOffset = DataStart + pos;
			}
			break;

		case 0x00020001:
			// header is 4 dwords + external data
			{
				int d1, d2, pos;
				*Ar << d1 << d2 << pos;
				Entry->DataOffset = DataStart + pos;
			}
			break;

		default:
			// header is 2 dwords - offset and size + external data
			{
				int pos;
				*Ar << pos;
				Entry->DataOffset = DataStart + pos;
			}
			break;
		}
	}
	// setup sizes of blocks placed after DataStart (not embedded into file list)
	for (i = 0; i < DataCount; i++)
	{
		XprEntry *Entry = &Info->Items[i];
		if (Entry->DataOffset < DataStart) continue; // embedded data
		// Entry points to a data block placed after DataStart position
		// we should find a next block
		int NextPos = FileLen;
		for (int j = i + 1; j < DataCount; j++)
		{
			XprEntry *NextEntry = &Info->Items[j];
			if (NextEntry->DataOffset < DataStart) continue; // embedded data
			NextPos = NextEntry->DataOffset;
			break;
		}
		Entry->DataSize = NextPos - Entry->DataOffset;
	}
#if XPR_DEBUG
	for (i = 0; i < DataCount; i++)
	{
		XprEntry *Entry = &Info->Items[i];
		printf("  %3d %08X [%08X] .. %08X  %s\n", i, Entry->DataOffset, Entry->DataSize, Entry->DataOffset + Entry->DataSize, Entry->Name);
	}
#endif

	delete Ar;
	return true;

	unguardf(("%s", file->RelativeName));
}


byte *FindXprData(const char *Name, int *DataSize)
{
	// scan xprs
	static bool ready = false;
	if (!ready)
	{
		ready = true;
		appEnumGameFiles(ReadXprFile, "xpr");
	}
	// find a file
	for (int i = 0; i < xprFiles.Num(); i++)
	{
		XprInfo *Info = &xprFiles[i];
		for (int j = 0; j < Info->Items.Num(); j++)
		{
			XprEntry *File = &Info->Items[j];
			if (strcmp(File->Name, Name) == 0)
			{
				// found
				printf("Loading stream %s from %s (%d bytes)\n", Name, Info->File->RelativeName, File->DataSize);
				FArchive *Reader = appCreateFileReader(Info->File);
				Reader->Seek(File->DataOffset);
				byte *buf = new byte[File->DataSize];
				Reader->Serialize(buf, File->DataSize);
				delete Reader;
				if (DataSize) *DataSize = File->DataSize;
				return buf;
			}
		}
	}
	printf("WARNING: external stream %s was not found\n", Name);
	if (DataSize) *DataSize = 0;
	return NULL;
}

#endif // UC2

#if BIOSHOCK

//#define DUMP_BIO_CATALOG			1
//#define DEBUG_BIO_BULK				1

struct BioBulkCatalogItem
{
	FString				ObjectName;
	FString				PackageName;
	int					f10;				// always 0
	int					DataOffset;
	int					DataSize;
	int					DataSize2;			// the same as DataSize
	int					f20;

	friend FArchive& operator<<(FArchive &Ar, BioBulkCatalogItem &S)
	{
		Ar << S.ObjectName << S.PackageName << S.f10 << S.DataOffset << S.DataSize << S.DataSize2 << S.f20;
		assert(S.f10 == 0);
//		assert(S.DataSize == S.DataSize2);	-- the same on PC, but not the same on XBox360
#if DUMP_BIO_CATALOG
		printf("  %s / %s - %08X:%08X %X %X %X\n", *S.ObjectName, *S.PackageName, S.f10, S.DataOffset, S.DataSize, S.DataSize2, S.f20);
#endif
		return Ar;
	}
};


struct BioBulkCatalogFile
{
	int64				f0;
	FString				Filename;
	TArray<BioBulkCatalogItem> Items;

	friend FArchive& operator<<(FArchive &Ar, BioBulkCatalogFile &S)
	{
		Ar << S.f0 << S.Filename;
#if DUMP_BIO_CATALOG
		printf("<<< %s >>>\n", *S.Filename);
#endif
		Ar << S.Items;
#if DEBUG_BIO_BULK
		int minS2 = 99999999, maxS2 = -99999999, min20 = 99999999, max20 = -99999999;
		for (int i = 0; i < S.Items.Num(); i++)
		{
			int n1 = S.Items[i].DataSize2;
			if (n1 < minS2) minS2 = n1;
			if (n1 > maxS2) maxS2 = n1;
			int n2 = S.Items[i].f20;
			if (n2 < min20) min20 = n1;
			if (n2 > max20) max20 = n1;
		}
		printf("DS2=%X..%X  f20=%X..%X", minS2, maxS2, min20, max20);
#endif // DEBUG_BIO_BULK
		return Ar;
	}
};

struct BioBulkCatalog
{
	byte				Endian;	//?? or Platform: 0=PC, 1=XBox360, 2=PS3?
	int64				f4;
	int					fC;
	TArray<BioBulkCatalogFile> Files;

	friend FArchive& operator<<(FArchive &Ar, BioBulkCatalog &S)
	{
		Ar << S.Endian;
		if (S.Endian) Ar.ReverseBytes = true;
		Ar << S.f4 << S.fC << S.Files;
		return Ar;
	}
};

static TArray<BioBulkCatalog> bioCatalog;

static bool BioReadBulkCatalogFile(const CGameFileInfo *file)
{
	guard(BioReadBulkCatalogFile);
	FArchive *Ar = appCreateFileReader(file);
	// setup for reading Bioshock data
	Ar->ArVer         = 141;
	Ar->ArLicenseeVer = 0x38;
	Ar->Game          = GAME_Bioshock;
	// serialize
	printf("Reading %s\n", file->RelativeName);
	BioBulkCatalog *cat = new (bioCatalog) BioBulkCatalog;
	*Ar << *cat;
	// finalize
	delete Ar;
	return true;
	unguardf(("%s", file->RelativeName));
}


static void BioReadBulkCatalog()
{
	static bool ready = false;
	if (ready) return;
	ready = true;
	appEnumGameFiles(BioReadBulkCatalogFile, "bdc");
	if (!bioCatalog.Num()) printf("WARNING: no *.bdc files found\n");
}

static byte *FindBioTexture(const UTexture *Tex)
{
	int needSize = Tex->CachedBulkDataSize & 0xFFFFFFFF;
#if DEBUG_BIO_BULK
	printf("Search for ... %s (size=%X)\n", Tex->Name, needSize);
#endif
	BioReadBulkCatalog();
	for (int i = 0; i < bioCatalog.Num(); i++)
	{
		BioBulkCatalog &Cat = bioCatalog[i];
	for (int j = 0; j < Cat.Files.Num(); j++)
	{
		const BioBulkCatalogFile &File = Cat.Files[j];
		for (int k = 0; k < File.Items.Num(); k++)
		{
			const BioBulkCatalogItem &Item = File.Items[k];
			if (!strcmp(Tex->Name, Item.ObjectName))
			{
				if (abs(needSize - Item.DataSize) > 0x4000)		// differs in 16k
				{
#if DEBUG_BIO_BULK
					printf("... Found %s in %s with wrong BulkDataSize %X (need %X)\n", Tex->Name, *File.Filename, Item.DataSize, needSize);
#endif
					continue;
				}
#if DEBUG_BIO_BULK
				printf("... Found %s in %s at %X size %X (%dx%d fmt=%d bpp=%g strip:%d mips:%d)\n", Tex->Name, *File.Filename, Item.DataOffset, Item.DataSize,
					Tex->USize, Tex->VSize, Tex->Format, (float)Item.DataSize / (Tex->USize * Tex->VSize),
					Tex->HasBeenStripped, Tex->StrippedNumMips);
#endif
				// found
				const CGameFileInfo *bulkFile = appFindGameFile(File.Filename);
				if (!bulkFile)
				{
					// no bulk file
					printf("Decompressing %s: %s is missing\n", Tex->Name, *File.Filename);
					return NULL;
				}

				printf("Reading %s mip level %d (%dx%d) from %s\n", Tex->Name, 0, Tex->USize, Tex->VSize, bulkFile->RelativeName);
				FArchive *Reader = appCreateFileReader(bulkFile);
				Reader->Seek(Item.DataOffset);
				byte *buf = new byte[max(Item.DataSize, needSize)];
				Reader->Serialize(buf, Item.DataSize);
				delete Reader;
				return buf;
			}
		}
	}
	}
#if DEBUG_BIO_BULK
	printf("... Bulk for %s was not found\n", Tex->Name);
#endif
	return NULL;
}

#endif // BIOSHOCK

byte *UTexture::Decompress(int &USize, int &VSize) const
{
	guard(UTexture::Decompress);
	const byte* pic = NULL;
	byte* picFree   = NULL;
	int DataSize    = 0;
#if BIOSHOCK
	if (Package && Package->Game == GAME_Bioshock && CachedBulkDataSize) //?? check bStripped or Baked ?
	{
		BioReadBulkCatalog();
		pic = picFree = FindBioTexture(this);
		if (pic)
		{
			USize = this->USize;
			VSize = this->VSize;
			DataSize = CachedBulkDataSize;
		}
	}
#endif // BIOSHOCK
#if UC2
	if (Package && Package->Engine() == GAME_UE2X)
	{
		// try to find texture inside XBox xpr files
		pic = picFree = FindXprData(Name, NULL);
		if (pic)
		{
			USize = this->USize;
			VSize = this->VSize;
		}
	}
#endif // UC2
	if (!pic)		// texture is not taken from external source
	{
		for (int n = 0; n < Mips.Num(); n++)
		{
			// find 1st mipmap with non-null data array
			// reference: DemoPlayerSkins.utx/DemoSkeleton have null-sized 1st 2 mips
			const FMipmap &Mip = Mips[n];
			if (!Mip.DataArray.Num())
				continue;
			USize    = Mip.USize;
			VSize    = Mip.VSize;
			pic      = &Mip.DataArray[0];
			DataSize = Mip.DataArray.Num();
			break;
		}
	}
	// decompress texture
	if (pic)
	{
		byte *pic2 = NULL;
#if BIOSHOCK && XBOX360
		if (Package && Package->Game == GAME_Bioshock && Package->Platform == PLATFORM_XBOX360)
		{
//			printf("fmt=%s  bulk=%d  w=%d  h=%d\n", EnumToName("EPixelFormat", Format), DataSize, USize, VSize);
			char* Error;
			pic2 = DecompressXBox360(pic, DataSize, Format, USize, VSize, Name, Error, Package->Game);
			if (!pic2)
			{
				const char *FmtName = EnumToName("ETextureFormat", Format);
				if (!FmtName) FmtName = "???";
				appNotify("ERROR UTexture::DecompressXBox360: %s'%s' (%s=%d): %s",
					GetClassName(), Name,
					FmtName, Format,
					Error
				);
			}
			if (picFree) delete picFree;
			return pic2;
		}
#endif // BIOSHOCK && XBOX360
		pic2 = DecompressTexture(pic, USize, VSize, Format, Name, Palette);
		if (picFree) delete picFree;
		return pic2;
	}
	// no valid mipmaps
	return NULL;
	unguard;
}


#if UNREAL3

/*-----------------------------------------------------------------------------
	UTexture2D::Decompress (UE3)
-----------------------------------------------------------------------------*/

#if DCU_ONLINE

// string hashing
static unsigned GCRCTable[256];

static void BuildCRCTable()
{
	assert(!GCRCTable[0]);
    for (int i = 0; i < 256; i++)
	{
		unsigned hash = i << 24;
		for (int j = 0; j < 8; j++)
		{
			if (hash & 0x80000000)
				hash = (hash << 1) ^ 0x04C11DB7;
			else
				hash <<= 1;
		}
		GCRCTable[i] = hash;
	}
}

static unsigned appStrihash(const char *str)
{
	if (!GCRCTable[0]) BuildCRCTable();

	unsigned Hash = 0;
	while (*str)
	{
		byte B = toupper(*str++);
		Hash   = ((Hash >> 8) & 0x00FFFFFF) ^ GCRCTable[(Hash ^ B) & 0x000000FF];
		B      = 0;		// simulate unicode
		Hash   = ((Hash >> 8) & 0x00FFFFFF) ^ GCRCTable[(Hash ^ B) & 0x000000FF];
	}
	return Hash;
}

// UTextureFileCacheRemap class

struct FTfcRemapEntry_DCU
{
	unsigned		Hash;
	int				Offset;
	FString			CollisionString;

	friend FArchive& operator<<(FArchive &Ar, FTfcRemapEntry_DCU &R)
	{
		guard(FTfcRemapEntry_DCU<<);
		Ar << R.Hash << R.Offset << R.CollisionString;
		if (R.CollisionString.Num() > 1) appNotify("HASH COLLISION: %s\n", *R.CollisionString);	//!!
		return Ar;
		unguard;
	}
};

class UTextureFileCacheRemap : public UObject
{
	DECLARE_CLASS(UTextureFileCacheRemap, UObject);
public:
	TArray<FTfcRemapEntry_DCU> TextureHashToRemapOffset;	// really - TMultiMap<unsigned, TFC_Remap_Info>

	void Serialize(FArchive &Ar)
	{
		guard(UTextureFileCacheRemap::Serialize);

		Super::Serialize(Ar);
		Ar << TextureHashToRemapOffset;

		unguard;
	}
};


// Main worker function
static int GetRealTextureOffset_DCU_2(unsigned Hash, const char *TFCName)
{
	guard(GetRealTextureOffset_DCU_2);

	static bool classRegistered = false;
	if (!classRegistered)
	{
		BEGIN_CLASS_TABLE
			REGISTER_CLASS(UTextureFileCacheRemap)
		END_CLASS_TABLE
		classRegistered = true;
	}

	// code to load object TextureFileCacheRemap from the package <TFC_NAME>_Remap

	// load a package
	char PkgName[256];
	appSprintf(ARRAY_ARG(PkgName), "%s_Remap", TFCName);
	UnPackage *Package = UnPackage::LoadPackage(PkgName);
	if (!Package)
	{
		printf("Package %s was not found\n", PkgName);
		return -1;
	}
	// find object ...
	int mapExportIdx = Package->FindExport("TextureFileCacheRemap", "TextureFileCacheRemap");
	if (mapExportIdx == INDEX_NONE)
	{
		printf("ERROR: unable to find export \"TextureFileCacheRemap\" in package %s\n", PkgName);
		return -1;
	}
	// ... and load it
	const UTextureFileCacheRemap *Remap = static_cast<UTextureFileCacheRemap*>(Package->CreateExport(mapExportIdx));
	assert(Remap);
	int Offset = -1;
	for (int i = 0; i < Remap->TextureHashToRemapOffset.Num(); i++)
	{
		const FTfcRemapEntry_DCU &E = Remap->TextureHashToRemapOffset[i];
		if (E.Hash == Hash)
		{
			Offset = E.Offset;
			break;
		}
	}
	return Offset;

	unguardf(("TFC=%s Hash=%08X", TFCName, Hash));
}


static int GetRealTextureOffset_DCU(const UTexture2D *Obj)
{
	guard(GetRealTextureOffset_DCU);

	char ObjName[256];
	Obj->GetFullName(ARRAY_ARG(ObjName));
	unsigned Hash = appStrihash(ObjName);
	const char *TFCName = Obj->TextureFileCacheName;
	return GetRealTextureOffset_DCU_2(Hash, TFCName);

	unguardf(("%s.%s", Obj->Package->Name, Obj->Name));
}


UUIStreamingTextures::~UUIStreamingTextures()
{
	// free generated data
	int i;
	for (i = 0; i < Textures.Num(); i++)
		delete Textures[i];
	for (i = 0; i < Names.Num(); i++)
		free(Names[i]);				// allocated with strdup()
}


void UUIStreamingTextures::PostLoad()
{
	guard(UUIStreamingTextures::PostLoad);

	assert(Package->Game == GAME_DCUniverse);
	// code is similar to Rune's USkelModel::Serialize()
	printf("Creating %d UI textures ...\n", TextureHashToInfo.Num());
	for (int i = 0; i < TextureHashToInfo.Num(); i++)
	{
		// create new UTexture2D
		const UIStreamingTexture_Info_DCU &S = TextureHashToInfo[i];
		UTexture2D *Tex = static_cast<UTexture2D*>(CreateClass("Texture2D"));
		char nameBuf[256];
		appSprintf(ARRAY_ARG(nameBuf), "UITexture_%08X", S.Hash);
		char *name = strdup(nameBuf);
		Textures.AddItem(Tex);
		Names.AddItem(name);
		// setup UOnject
		Tex->Name         = name;
		Tex->Package      = Package;
		Tex->PackageIndex = INDEX_NONE;		// not really exported
		Tex->TextureFileCacheName = S.TextureFileCacheName;
		Tex->Format       = (EPixelFormat)S.Format;
		Tex->SizeX        = S.nWidth;
		Tex->SizeY        = S.nHeight;
//		Tex->SRGB         = S.bSRGB;
		// create mipmap
		FTexture2DMipMap *Mip = new (Tex->Mips) FTexture2DMipMap;
		Mip->SizeX        = S.nWidth;
		Mip->SizeY        = S.nHeight;
		Mip->Data.BulkDataFlags        = S.BulkDataFlags;
		Mip->Data.ElementCount         = S.ElementCount;
		Mip->Data.BulkDataSizeOnDisk   = S.BulkDataSizeOnDisk;
		Mip->Data.BulkDataOffsetInFile = S.BulkDataOffsetInFile;
		// find TFC remap
//		unsigned Hash = appStrihash("UIICONS101_I1.dds");	//??
//		printf("Hash: %08X\n", Hash);
		if (Mip->Data.BulkDataOffsetInFile < 0)
		{
			int Offset = GetRealTextureOffset_DCU_2(S.Hash, S.TextureFileCacheName);
			if (Offset < 0)
				Mip->Data.BulkDataFlags = BULKDATA_NoData;
			else
				Mip->Data.BulkDataOffsetInFile = Offset - Mip->Data.BulkDataOffsetInFile - 1;
			printf("OFFS: %X\n", Mip->Data.BulkDataOffsetInFile);
		}
#if 1
		printf("%d: %s  {%08X} %dx%d %s [%08X + %08X]\n", i, *S.TextureFileCacheName, S.Hash,
			S.nWidth, S.nHeight, EnumToName("EPixelFormat", Tex->Format),
			S.BulkDataOffsetInFile, S.BulkDataSizeOnDisk
		);
#endif
	}
	printf("... done\n");

	unguard;
}

#endif // DCU_ONLINE

bool UTexture2D::LoadBulkTexture(int MipIndex) const
{
	const CGameFileInfo *bulkFile = NULL;

	guard(UTexture2D::LoadBulkTexture);

	const FTexture2DMipMap &Mip = Mips[MipIndex];

	// Here: data is either in TFC file or in other package
	const char *bulkFileName = NULL, *bulkFileExt = NULL;
	if (stricmp(TextureFileCacheName, "None") != 0)
	{
		// TFC file is assigned
		//!! cache checking of tfc file(s) + cache handles (FFileReader)
		//!! note #1: there can be few cache files!
		//!! note #2: XMen (PC) has renamed tfc file after cooking (TextureFileCacheName value is wrong)
		bulkFileName = TextureFileCacheName;
		bulkFileExt  = "tfc";
		bulkFile = appFindGameFile(bulkFileName, bulkFileExt);
		if (!bulkFile)
			bulkFile = appFindGameFile(bulkFileName, "xxx");	// sometimes tfc has xxx extension
	}
	else
	{
		// data is inside another package
		//!! copy-paste from UnPackage::CreateExport(), should separate function
		// find outermost package
		if (this->PackageIndex)
		{
			int PackageIndex = this->PackageIndex;		// export entry for this object (UTexture2D)
			while (true)
			{
				const FObjectExport &Exp2 = Package->GetExport(PackageIndex);
				if (!Exp2.PackageIndex) break;			// get parent (UPackage)
				PackageIndex = Exp2.PackageIndex - 1;	// subtract 1 from package index
			}
			const FObjectExport &Exp2 = Package->GetExport(PackageIndex);
			if (Exp2.ExportFlags & EF_ForcedExport)
			{
				bulkFileName = Exp2.ObjectName;
				bulkFileExt  = NULL;					// find package file
//				printf("BULK: %s (%X)\n", *Exp2.ObjectName, Exp2.ExportFlags);
			}
		}
		if (!bulkFileName) return false;					// just in case
		bulkFile = appFindGameFile(bulkFileName, bulkFileExt);
	}

	if (!bulkFile)
	{
		printf("Decompressing %s: %s.%s is missing\n", Name, bulkFileName, bulkFileExt ? bulkFileExt : "*");
		return false;
	}

	printf("Reading %s mip level %d (%dx%d) from %s\n", Name, MipIndex, Mip.SizeX, Mip.SizeY, bulkFile->RelativeName);
	FArchive *Ar = appCreateFileReader(bulkFile);
	Ar->SetupFrom(*Package);
	FByteBulkData *Bulk = const_cast<FByteBulkData*>(&Mip.Data);	//!! const_cast
#if DCU_ONLINE
	if (Package->Game == GAME_DCUniverse && Bulk->BulkDataOffsetInFile < 0)
	{
		int Offset = GetRealTextureOffset_DCU(this);
		if (Offset < 0) return false;
		Bulk->BulkDataOffsetInFile = Offset - Bulk->BulkDataOffsetInFile - 1;
//		printf("OFFS: %X\n", Bulk->BulkDataOffsetInFile);
	}
#endif // DCU_ONLINE
//	printf("%X %X [%d] f=%X\n", Bulk, Bulk->BulkDataOffsetInFile, Bulk->ElementCount, Bulk->BulkDataFlags);
	Bulk->SerializeChunk(*Ar);
	delete Ar;
	return true;

	unguardf(("File=%s", bulkFile ? bulkFile->RelativeName : "none"));
}


byte *UTexture2D::Decompress(int &USize, int &VSize) const
{
	guard(UTexture2D::Decompress);

	bool bulkChecked = false;
	for (int n = 0; n < Mips.Num(); n++)
	{
		// find 1st mipmap with non-null data array
		// reference: DemoPlayerSkins.utx/DemoSkeleton have null-sized 1st 2 mips
		const FTexture2DMipMap &Mip = Mips[n];
		if (!Mip.Data.BulkData)
		{
			//?? Separate this function ?
			// check for external bulk
			//!! * -notfc cmdline switch
			//!! * material viewer: support switching mip levels (for xbox decompression testing)
			if (Mip.Data.BulkDataFlags & BULKDATA_NoData) continue;		// mip level is stripped
			if (!(Mip.Data.BulkDataFlags & BULKDATA_StoreInSeparateFile)) continue;
			// some optimization in a case of missing bulk file
			if (bulkChecked) continue;									// already checked for previous mip levels
			bulkChecked = true;
			if (!LoadBulkTexture(n)) continue;
		}
		USize = Mip.SizeX;
		VSize = Mip.SizeY;
		ETextureFormat intFormat;
		if (Format == PF_A8R8G8B8)
			intFormat = TEXF_RGBA8;
		else if (Format == PF_DXT1)
			intFormat = TEXF_DXT1;
		else if (Format == PF_DXT3)
			intFormat = TEXF_DXT3;
		else if (Format == PF_DXT5)
			intFormat = TEXF_DXT5;
		else if (Format == PF_G8)
			intFormat = TEXF_L8;
		else if (Format == PF_V8U8)
			intFormat = TEXF_CxV8U8;
		else if (Format == PF_BC5)
			intFormat = TEXF_3DC;
#if MASSEFF
//??		else if (Format == PF_NormapMap_LQ) -- seems not used
//??			intFormat = TEXF_3DC;
		else if (Format == PF_NormalMap_HQ)
			intFormat = TEXF_3DC;
#endif // MASSEFF
		else
		{
			const char *FmtName = EnumToName("EPixelFormat", Format);
			if (!FmtName) FmtName = "???";
			appNotify("Unknown texture format: %s (%d)", FmtName, Format);
			return NULL;
		}

#if IPHONE
		if (Package->Platform == PLATFORM_IOS)
		{
			if (intFormat == TEXF_DXT1)
				intFormat = bForcePVRTC4 ? TEXF_PVRTC4 : TEXF_PVRTC2;
			if (intFormat == TEXF_DXT5)
				intFormat = TEXF_PVRTC4;
		}
#endif // IPHONE

		byte* ret = NULL;
		switch (Package->Platform)
		{
#if XBOX360
		case PLATFORM_XBOX360:
			{
				const FByteBulkData &Bulk = Mip.Data;
				int DataSize = Bulk.ElementCount * Bulk.GetElementSize();
//				printf("fmt=%s  bulk=%d  w=%d  h=%d\n", EnumToName("EPixelFormat", Format), DataSize, USize, VSize);
				char* Error;
				ret = DecompressXBox360(Bulk.BulkData, DataSize, intFormat, USize, VSize, Name, Error, Package->Game);
				if (!ret)
				{
					const char *FmtName = EnumToName("EPixelFormat", Format);
					if (!FmtName) FmtName = "???";
					appNotify("ERROR UTexture2D::DecompressXBox360: %s'%s' (%s=%d): %s",
						GetClassName(), Name,
						FmtName, Format,
						Error
					);
				}
			}
			break;
#endif // XBOX360
		default:
			ret = DecompressTexture(Mip.Data.BulkData, USize, VSize, intFormat, Name, NULL);
			break;
		}
		if (!ret) return NULL;			// failed to decompress, nothing to check more

#if 0
		// check for possible texture modification using UnpackMin/UnpackMax
		if (UnpackMin[0] == 0 && UnpackMin[1] == 0 && UnpackMin[1] == 0)
			return ret;					// simple texture

		// possibly a normalmap - unpacks to -1..+1 range
		unsigned Xor = 0;
		int i;
		for (i = 0; i < 4; i++)
			if (UnpackMin[i] > UnpackMax[i])
				Xor |= 0xFF << (i * 8);	// "N xor 255 = 255 - N" (for byte N)
		// original texture is BGRA, our is RGBA
		if (!Xor) return ret;			// do not require processing
		unsigned *upic = (unsigned*)ret;
		for (i = 0; i < USize * VSize; i++, upic++)
			*upic = *upic ^ Xor;
#endif
		return ret;
	} // enf of mipmap iteration
	// no valid mipmaps
	return NULL;

	unguardf(("Tex=%s", Name));
}

#endif // UNREAL3