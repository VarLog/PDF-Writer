#include "TrueTypeFileInput.h"
#include "Trace.h"
#include "InputFile.h"

TrueTypeFileInput::TrueTypeFileInput(void)
{
	mHMtx = NULL;
	mName.mNameEntries = NULL;
	mLoca = NULL;
	mGlyf = NULL;
}

TrueTypeFileInput::~TrueTypeFileInput(void)
{
	FreeTables();
}

void TrueTypeFileInput::FreeTables()
{
	delete[] mHMtx;
	mHMtx = NULL;
	if(mName.mNameEntries)
	{
		for(unsigned short i =0; i < mName.mNameEntriesCount; ++i)
			delete[] mName.mNameEntries[i].String;
	}
	delete[] mName.mNameEntries;
	mName.mNameEntries = NULL;
	delete[] mLoca;
	mLoca = NULL;
	delete[] mGlyf;
	mGlyf = NULL;

	UShortToGlyphEntryMap::iterator it = mActualGlyphs.begin();
	for(; it != mActualGlyphs.end(); ++it)
		delete it->second;
	mActualGlyphs.clear();
}

EStatusCode TrueTypeFileInput::ReadTrueTypeFile(const wstring& inFontFilePath)
{
	InputFile fontFile;

	EStatusCode status = fontFile.OpenFile(inFontFilePath);
	if(status != eSuccess)
	{
		TRACE_LOG1("TrueTypeFileInput::ReadTrueTypeFile, cannot open true type font file at %s",inFontFilePath.c_str());
		return status;
	}

	status = ReadTrueTypeFile(fontFile.GetInputStream());
	fontFile.CloseFile();
	return status;
}

EStatusCode TrueTypeFileInput::ReadTrueTypeFile(IByteReader* inTrueTypeFile)
{
	EStatusCode status;

	do
	{
		FreeTables();

		mPrimitivesReader.SetTrueTypeStream(inTrueTypeFile);

		status = ReadTrueTypeHeader();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReadTrueTypeFile, failed to read true type header");
			break;
		}
		
		status = ReadHead();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReadTrueTypeFile, failed to read head table");
			break;
		}
		
		status = ReadMaxP();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReadTrueTypeFile, failed to read maxp table");
			break;
		}

		status = ReadHHea();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReadTrueTypeFile, failed to read hhea table");
			break;
		}

		status = ReadHMtx();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReadTrueTypeFile, failed to read hmtx table");
			break;
		}

		status = ReadOS2();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReadTrueTypeFile, failed to read os2 table");
			break;
		}

		status = ReadName();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReadTrueTypeFile, failed to read name table");
			break;
		}

		// true type specifics

		status = ReadLoca();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReadTrueTypeFile, failed to read loca table");
			break;
		}

		status = ReadGlyfForDependencies();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReadTrueTypeFile, failed to read glyf table");
			break;
		}
		mCVTExists = mTables.find(GetTag("cvt ")) != mTables.end();
		mFPGMExists = mTables.find(GetTag("fpgm")) != mTables.end();
		mPREPExists = mTables.find(GetTag("prep")) != mTables.end();

	}while(false);

	return status;
}

EStatusCode TrueTypeFileInput::ReadTrueTypeHeader()
{
	EStatusCode status;
	TableEntry tableEntry;
	unsigned long tableTag;

	do
	{
		status = VerifyTrueTypeSFNT();
		if(status != eSuccess)
		{
			TRACE_LOG("TrueTypeFileInput::ReaderTrueTypeHeader, SFNT header not true type");
			break;
		}

		mPrimitivesReader.ReadUSHORT(mTablesCount);
		// skip the next 6. i don't give a rats...
		mPrimitivesReader.Skip(6);

		for(unsigned short i = 0; i < mTablesCount; ++i)
		{
			mPrimitivesReader.ReadULONG(tableTag);
			mPrimitivesReader.ReadULONG(tableEntry.CheckSum);
			mPrimitivesReader.ReadULONG(tableEntry.Offset);
			mPrimitivesReader.ReadULONG(tableEntry.Length);
			mTables.insert(ULongToTableEntryMap::value_type(tableTag,tableEntry));
		}
		status = mPrimitivesReader.GetInternalState();
	}
	while(false);

	return status;
}

EStatusCode TrueTypeFileInput::VerifyTrueTypeSFNT()
{
	unsigned long sfntVersion;

	mPrimitivesReader.ReadULONG(sfntVersion);

	return ((0x10000 == sfntVersion) || (0x74727565 /* true */ == sfntVersion)) ? eSuccess : eFailure;
}

unsigned long TrueTypeFileInput::GetTag(const char* inTagName)
{
	Byte buffer[4];
	int i=0;

	for(; i<strlen(inTagName);++i)
		buffer[i] = (Byte)inTagName[i];
	for(;i<4;++i)
		buffer[i] = 0x20;

	return	((unsigned long)buffer[0]<<24) + ((unsigned long)buffer[1]<<16) + 
			((unsigned long)buffer[2]<<8) + buffer[3];
}

EStatusCode TrueTypeFileInput::ReadHead()
{
	ULongToTableEntryMap::iterator it = mTables.find(GetTag("head"));
	if(it == mTables.end())
	{
		TRACE_LOG("TrueTypeFileInput::ReadHead, could not find head table");
		return eFailure;
	}

	mPrimitivesReader.SetOffset(it->second.Offset);
	mPrimitivesReader.ReadFixed(mHead.TableVersionNumber);
	mPrimitivesReader.ReadFixed(mHead.FontRevision);
	mPrimitivesReader.ReadULONG(mHead.CheckSumAdjustment);
	mPrimitivesReader.ReadULONG(mHead.MagicNumber);
	mPrimitivesReader.ReadUSHORT(mHead.Flags);
	mPrimitivesReader.ReadUSHORT(mHead.UnitsPerEm);
	mPrimitivesReader.ReadLongDateTime(mHead.Created);
	mPrimitivesReader.ReadLongDateTime(mHead.Modified);
	mPrimitivesReader.ReadSHORT(mHead.XMin);
	mPrimitivesReader.ReadSHORT(mHead.YMin);
	mPrimitivesReader.ReadSHORT(mHead.XMax);
	mPrimitivesReader.ReadSHORT(mHead.YMax);
	mPrimitivesReader.ReadUSHORT(mHead.MacStyle);
	mPrimitivesReader.ReadUSHORT(mHead.LowerRectPPEM);
	mPrimitivesReader.ReadSHORT(mHead.FontDirectionHint);
	mPrimitivesReader.ReadSHORT(mHead.IndexToLocFormat);
	mPrimitivesReader.ReadSHORT(mHead.GlyphDataFormat);

	return mPrimitivesReader.GetInternalState();	
}

EStatusCode TrueTypeFileInput::ReadMaxP()
{
	ULongToTableEntryMap::iterator it = mTables.find(GetTag("maxp"));
	if(it == mTables.end())
	{
		TRACE_LOG("TrueTypeFileInput::ReadMaxP, could not find maxp table");
		return eFailure;
	}
	mPrimitivesReader.SetOffset(it->second.Offset);

	memset(&mMaxp,0,sizeof(MaxpTable)); // set all with 0's in case the table's too short, so we'll have nice lookin values

	mPrimitivesReader.ReadFixed(mMaxp.TableVersionNumber);
	mPrimitivesReader.ReadUSHORT(mMaxp.NumGlyphs);

	if(1.0 == mMaxp.TableVersionNumber)
	{
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxPoints);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxCountours);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxCompositePoints);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxCompositeContours);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxZones);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxTwilightPoints);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxStorage);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxFunctionDefs);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxInstructionDefs);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxStackElements);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxSizeOfInstructions);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxComponentElements);
		mPrimitivesReader.ReadUSHORT(mMaxp.MaxCompontentDepth);
	}
	return mPrimitivesReader.GetInternalState();	

}

EStatusCode TrueTypeFileInput::ReadHHea()
{
	ULongToTableEntryMap::iterator it = mTables.find(GetTag("hhea"));
	if(it == mTables.end())
	{
		TRACE_LOG("TrueTypeFileInput::ReadHHea, could not find hhea table");
		return eFailure;
	}

	mPrimitivesReader.SetOffset(it->second.Offset);

	mPrimitivesReader.ReadFixed(mHHea.TableVersionNumber);
	mPrimitivesReader.ReadSHORT(mHHea.Ascender);
	mPrimitivesReader.ReadSHORT(mHHea.Descender);
	mPrimitivesReader.ReadSHORT(mHHea.LineGap);
	mPrimitivesReader.ReadUSHORT(mHHea.AdvanceWidthMax);
	mPrimitivesReader.ReadSHORT(mHHea.MinLeftSideBearing);
	mPrimitivesReader.ReadSHORT(mHHea.MinRightSideBearing);
	mPrimitivesReader.ReadSHORT(mHHea.XMaxExtent);
	mPrimitivesReader.ReadSHORT(mHHea.CaretSlopeRise);
	mPrimitivesReader.ReadSHORT(mHHea.CaretSlopeRun);
	mPrimitivesReader.ReadSHORT(mHHea.CaretOffset);
	mPrimitivesReader.Skip(8);
	mPrimitivesReader.ReadSHORT(mHHea.MetricDataFormat);
	mPrimitivesReader.ReadUSHORT(mHHea.NumberOfHMetrics);

	return mPrimitivesReader.GetInternalState();	
}

EStatusCode TrueTypeFileInput::ReadHMtx()
{
	ULongToTableEntryMap::iterator it = mTables.find(GetTag("hmtx"));
	if(it == mTables.end())
	{
		TRACE_LOG("TrueTypeFileInput::ReadHMtx, could not find hmtx table");
		return eFailure;
	}

	mPrimitivesReader.SetOffset(it->second.Offset);

	mHMtx = new HMtxTableEntry[mMaxp.NumGlyphs];

	unsigned int i=0;

	for(; i < mHHea.NumberOfHMetrics;++i)
	{
		mPrimitivesReader.ReadUSHORT(mHMtx[i].AdvanceWidth);
		mPrimitivesReader.ReadSHORT(mHMtx[i].LeftSideBearing);
	}

	for(; i < mMaxp.NumGlyphs; ++i)
	{
		mHMtx[i].AdvanceWidth = mHMtx[mHHea.NumberOfHMetrics-1].AdvanceWidth;
		mPrimitivesReader.ReadSHORT(mHMtx[i].LeftSideBearing);
	}

	return mPrimitivesReader.GetInternalState();	
}

EStatusCode TrueTypeFileInput::ReadOS2()
{
	ULongToTableEntryMap::iterator it = mTables.find(GetTag("OS/2"));
	if(it == mTables.end())
	{
		TRACE_LOG("TrueTypeFileInput::ReadOS2, could not find os2 table");
		return eFailure;
	}

	mPrimitivesReader.SetOffset(it->second.Offset);

	memset(&mOS2,0,sizeof(OS2Table));

	mPrimitivesReader.ReadUSHORT(mOS2.Version);
	mPrimitivesReader.ReadSHORT(mOS2.AvgCharWidth);
	mPrimitivesReader.ReadUSHORT(mOS2.WeightClass);
	mPrimitivesReader.ReadUSHORT(mOS2.WidthClass);
	mPrimitivesReader.ReadUSHORT(mOS2.FSType);

	mPrimitivesReader.ReadSHORT(mOS2.SubscriptXSize);
	mPrimitivesReader.ReadSHORT(mOS2.SubscriptYSize);
	mPrimitivesReader.ReadSHORT(mOS2.SubscriptXOffset);
	mPrimitivesReader.ReadSHORT(mOS2.SubscriptYOffset);
	mPrimitivesReader.ReadSHORT(mOS2.SuperscriptXSize);
	mPrimitivesReader.ReadSHORT(mOS2.SuperscriptYSize);
	mPrimitivesReader.ReadSHORT(mOS2.SuperscriptXOffset);
	mPrimitivesReader.ReadSHORT(mOS2.SuperscriptYOffset);
	mPrimitivesReader.ReadSHORT(mOS2.StrikeoutSize);
	mPrimitivesReader.ReadSHORT(mOS2.StrikeoutPosition);
	mPrimitivesReader.ReadSHORT(mOS2.FamilyClass);
	for(int i=0; i <10; ++i)
		mPrimitivesReader.ReadBYTE(mOS2.Panose[i]);
	mPrimitivesReader.ReadULONG(mOS2.UnicodeRange1);
	mPrimitivesReader.ReadULONG(mOS2.UnicodeRange2);
	mPrimitivesReader.ReadULONG(mOS2.UnicodeRange3);
	mPrimitivesReader.ReadULONG(mOS2.UnicodeRange4);
	for(int i=0; i <4; ++i)
		mPrimitivesReader.ReadCHAR(mOS2.AchVendID[i]);
	mPrimitivesReader.ReadUSHORT(mOS2.FSSelection);
	mPrimitivesReader.ReadUSHORT(mOS2.FirstCharIndex);
	mPrimitivesReader.ReadUSHORT(mOS2.LastCharIndex);
	mPrimitivesReader.ReadSHORT(mOS2.TypoAscender);
	mPrimitivesReader.ReadSHORT(mOS2.TypoDescender);
	mPrimitivesReader.ReadSHORT(mOS2.TypoLineGap);
	mPrimitivesReader.ReadUSHORT(mOS2.WinAscent);
	mPrimitivesReader.ReadUSHORT(mOS2.WinDescent);

	// version 1 OS/2 table may end here [see that there's enough to continue]
	if(it->second.Length >= (mPrimitivesReader.GetCurrentPosition() - it->second.Offset) + 18)
	{
		mPrimitivesReader.ReadULONG(mOS2.CodePageRange1);
		mPrimitivesReader.ReadULONG(mOS2.CodePageRange2);
		mPrimitivesReader.ReadSHORT(mOS2.XHeight);
		mPrimitivesReader.ReadSHORT(mOS2.CapHeight);
		mPrimitivesReader.ReadUSHORT(mOS2.DefaultChar);
		mPrimitivesReader.ReadUSHORT(mOS2.BreakChar);
		mPrimitivesReader.ReadUSHORT(mOS2.MaxContext);
	}
	return mPrimitivesReader.GetInternalState();	
}

EStatusCode TrueTypeFileInput::ReadName()
{
	ULongToTableEntryMap::iterator it = mTables.find(GetTag("name"));
	if(it == mTables.end())
	{
		TRACE_LOG("TrueTypeFileInput::ReadName, could not find name table");
		return eFailure;
	}

	mPrimitivesReader.SetOffset(it->second.Offset);	
	mPrimitivesReader.Skip(2);
	mPrimitivesReader.ReadUSHORT(mName.mNameEntriesCount);
	mName.mNameEntries = new NameTableEntry[mName.mNameEntriesCount];
	
	unsigned short stringOffset;
	
	mPrimitivesReader.ReadUSHORT(stringOffset);

	for(unsigned short i=0;i<mName.mNameEntriesCount;++i)
	{
		mPrimitivesReader.ReadUSHORT(mName.mNameEntries[i].PlatformID);
		mPrimitivesReader.ReadUSHORT(mName.mNameEntries[i].EncodingID);
		mPrimitivesReader.ReadUSHORT(mName.mNameEntries[i].LanguageID);
		mPrimitivesReader.ReadUSHORT(mName.mNameEntries[i].NameID);
		mPrimitivesReader.ReadUSHORT(mName.mNameEntries[i].Length);
		mPrimitivesReader.ReadUSHORT(mName.mNameEntries[i].Offset);
	}

	for(unsigned short i=0;i<mName.mNameEntriesCount;++i)
	{
		mName.mNameEntries[i].String = new char[mName.mNameEntries[i].Length];
		mPrimitivesReader.SetOffset(it->second.Offset + stringOffset + mName.mNameEntries[i].Offset);
		mPrimitivesReader.Read((Byte*)(mName.mNameEntries[i].String),mName.mNameEntries[i].Length);
	}

	return mPrimitivesReader.GetInternalState();	
}

EStatusCode TrueTypeFileInput::ReadLoca()
{
	ULongToTableEntryMap::iterator it = mTables.find(GetTag("loca"));
	if(it == mTables.end())
	{
		TRACE_LOG("TrueTypeFileInput::ReadLoca, could not find loca table");
		return eFailure;
	}
	mPrimitivesReader.SetOffset(it->second.Offset);	

	mLoca = new unsigned long[mMaxp.NumGlyphs+1];

	if(0 == mHead.IndexToLocFormat)
	{
		unsigned short buffer;
		for(unsigned short i=0; i < mMaxp.NumGlyphs+1; ++i)
		{
			mPrimitivesReader.ReadUSHORT(buffer);
			mLoca[i] = buffer << 1;
		}
	}
	else
	{
		for(unsigned short i=0; i < mMaxp.NumGlyphs+1; ++i)
			mPrimitivesReader.ReadULONG(mLoca[i]);
	}
	return mPrimitivesReader.GetInternalState();	
}

EStatusCode TrueTypeFileInput::ReadGlyfForDependencies()
{
	ULongToTableEntryMap::iterator it = mTables.find(GetTag("glyf"));
	if(it == mTables.end())
	{
		TRACE_LOG("TrueTypeFileInput::ReadGlyfForDependencies, could not find glyf table");
		return eFailure;
	}

	// it->second.Offset, is the offset to the beginning of the table
	mGlyf = new GlyphEntry*[mMaxp.NumGlyphs];

	for(unsigned short i=0; i < mMaxp.NumGlyphs; ++i)
	{
		if(mLoca[i+1] == mLoca[i])
		{
			mGlyf[i] = NULL;
		}
		else
		{
			mGlyf[i] = new GlyphEntry;

			mPrimitivesReader.SetOffset(it->second.Offset + mLoca[i]);
			mPrimitivesReader.ReadSHORT(mGlyf[i]->NumberOfContours);
			mPrimitivesReader.ReadSHORT(mGlyf[i]->XMin);
			mPrimitivesReader.ReadSHORT(mGlyf[i]->YMin);
			mPrimitivesReader.ReadSHORT(mGlyf[i]->XMax);
			mPrimitivesReader.ReadSHORT(mGlyf[i]->YMax);

			// Now look for dependencies
			if(mGlyf[i]->NumberOfContours < 0)
			{
				bool hasMoreComponents;
				unsigned short flags;
				unsigned short glyphIndex;

				do
				{
					mPrimitivesReader.ReadUSHORT(flags);
					mPrimitivesReader.ReadUSHORT(glyphIndex);
					mGlyf[i]->mComponentGlyphs.push_back(glyphIndex);
					if((flags & 1) != 0) // 
						mPrimitivesReader.Skip(4); // skip 2 shorts, ARG_1_AND_2_ARE_WORDS
					else
						mPrimitivesReader.Skip(2); // skip 1 short, nah - they are bytes

					if((flags & 8) != 0)
						mPrimitivesReader.Skip(2); // WE_HAVE_SCALE
					else if ((flags & 64) != 0)
						mPrimitivesReader.Skip(4); // WE_HAVE_AN_X_AND_Y_SCALE
					else if ((flags & 128) != 0)
						mPrimitivesReader.Skip(8); // WE_HAVE_A_TWO_BY_TWO

					hasMoreComponents = ((flags & 32) != 0);
				}while(hasMoreComponents);

			}

			mActualGlyphs.insert(UShortToGlyphEntryMap::value_type(i,mGlyf[i]));
		}
	}	


	return mPrimitivesReader.GetInternalState();	
}

unsigned short TrueTypeFileInput::GetGlyphsCount()
{
	return mMaxp.NumGlyphs;
}

TableEntry* TrueTypeFileInput::GetTableEntry(const char* inTagName)
{
	ULongToTableEntryMap::iterator it = mTables.find(GetTag(inTagName));
	
	if(it == mTables.end())	
		return NULL;
	else
		return &(it->second);
}