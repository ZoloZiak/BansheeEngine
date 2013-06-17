#include "CmFontImporter.h"
#include "CmFontImportOptions.h"
#include "CmPixelData.h"
#include "CmTexture.h"
#include "CmResources.h"
#include "CmDebug.h"
#include "CmTexAtlasGenerator.h"
#include "CmApplication.h"
#include "CmCoreThreadAccessor.h"

#include <ft2build.h>
#include <freetype/freetype.h>
#include FT_FREETYPE_H

namespace CamelotFramework
{
	FontImporter::FontImporter()
		:SpecificImporter() 
	{
		mExtensions.push_back("ttf");
		mExtensions.push_back("otf");
	}

	FontImporter::~FontImporter() 
	{

	}

	bool FontImporter::isExtensionSupported(const String& ext) const
	{
		String lowerCaseExt = ext;
		StringUtil::toLowerCase(lowerCaseExt);

		return find(mExtensions.begin(), mExtensions.end(), lowerCaseExt) != mExtensions.end();
	}

	bool FontImporter::isMagicNumberSupported(const UINT8* magicNumPtr, UINT32 numBytes) const
	{
		// TODO
		return false;
	}

	ImportOptionsPtr FontImporter::createImportOptions() const
	{
		return cm_shared_ptr<FontImportOptions, ScratchAlloc>();
	}

	HResource FontImporter::import(const String& filePath, ConstImportOptionsPtr importOptions)
	{
		const FontImportOptions* fontImportOptions = static_cast<const FontImportOptions*>(importOptions.get());

		FT_Library library;

		FT_Error error = FT_Init_FreeType(&library);
		if (error)
			CM_EXCEPT(InternalErrorException, "Error occurred during FreeType library initialization.");

		FT_Face face;
		error = FT_New_Face(library, filePath.c_str(), 0, &face);

		if (error == FT_Err_Unknown_File_Format)
		{
			CM_EXCEPT(InternalErrorException, "Failed to load font file: " + filePath + ". Unsupported file format.");
		}
		else if (error)
		{
			CM_EXCEPT(InternalErrorException, "Failed to load font file: " + filePath + ". Unknown error.");
		}

		Vector<std::pair<UINT32, UINT32>>::type charIndexRanges = fontImportOptions->getCharIndexRanges();
		Vector<UINT32>::type fontSizes = fontImportOptions->getFontSizes();
		UINT32 dpi = fontImportOptions->getDPI();

		FT_Int32 loadFlags = FT_LOAD_RENDER;
		if(!fontImportOptions->getAntialiasing())
			loadFlags |= FT_LOAD_TARGET_MONO | FT_LOAD_NO_AUTOHINT;

		Vector<FontData>::type dataPerSize;
		for(size_t i = 0; i < fontSizes.size(); i++)
		{
			FT_F26Dot6 ftSize = (FT_F26Dot6)(fontSizes[i] * (1 << 6));
			if(FT_Set_Char_Size( face, ftSize, 0, dpi, dpi))
				CM_EXCEPT(InternalErrorException, "Could not set character size." );

			FontData fontData;

			// Get all char sizes so we can generate texture layout
			Vector<TexAtlasElementDesc>::type atlasElements;
			Map<UINT32, UINT32>::type seqIdxToCharIdx;
			for(auto iter = charIndexRanges.begin(); iter != charIndexRanges.end(); ++iter)
			{
				for(UINT32 charIdx = iter->first; charIdx <= iter->second; charIdx++)
				{
					error = FT_Load_Char(face, (FT_ULong)charIdx, loadFlags);

					if(error)
						CM_EXCEPT(InternalErrorException, "Failed to load a character");

					FT_GlyphSlot slot = face->glyph;

					TexAtlasElementDesc atlasElement;
					atlasElement.input.width = slot->bitmap.width;
					atlasElement.input.height = slot->bitmap.rows;

					atlasElements.push_back(atlasElement);
					seqIdxToCharIdx[(UINT32)atlasElements.size() - 1] = charIdx;
				}
			}

			// Add missing glyph
			{
				error = FT_Load_Glyph(face, (FT_ULong)0, loadFlags);

				if(error)
					CM_EXCEPT(InternalErrorException, "Failed to load a character");

				FT_GlyphSlot slot = face->glyph;

				TexAtlasElementDesc atlasElement;
				atlasElement.input.width = slot->bitmap.width;
				atlasElement.input.height = slot->bitmap.rows;

				atlasElements.push_back(atlasElement);
			}

			// Create an optimal layout for character bitmaps
			TexAtlasGenerator texAtlasGen(false, MAXIMUM_TEXTURE_SIZE, MAXIMUM_TEXTURE_SIZE);
			Vector<TexAtlasPageDesc>::type pages = texAtlasGen.createAtlasLayout(atlasElements);

			INT32 baselineOffset = 0;
			UINT32 lineHeight = 0;

			// Create char bitmap atlas textures and load character information
			UINT32 pageIdx = 0;
			for(auto pageIter = pages.begin(); pageIter != pages.end(); ++pageIter)
			{
				UINT32 bufferSize = pageIter->width * pageIter->height * 2;

				// TODO - I don't actually need a 2 channel texture
				PixelData pixelData(pageIter->width, pageIter->height, 1, PF_R8G8);

				pixelData.allocateInternalBuffer();
				UINT8* pixelBuffer = pixelData.getData();
				memset(pixelBuffer, 0, bufferSize);

				for(size_t elementIdx = 0; elementIdx < atlasElements.size(); elementIdx++)
				{
					// Copy character bitmap
					if(atlasElements[elementIdx].output.page != pageIdx)
						continue;

					TexAtlasElementDesc curElement = atlasElements[elementIdx];
					bool isMissingGlypth = elementIdx == (atlasElements.size() - 1); // It's always the last element

					UINT32 charIdx = 0;
					if(!isMissingGlypth)
					{
						charIdx = seqIdxToCharIdx[(UINT32)elementIdx];

						error = FT_Load_Char(face, charIdx, loadFlags);
					}
					else
					{
						error = FT_Load_Glyph(face, 0, loadFlags);
					}

					if(error)
						CM_EXCEPT(InternalErrorException, "Failed to load a character");

					FT_GlyphSlot slot = face->glyph;

					if(slot->bitmap.buffer == nullptr && slot->bitmap.rows > 0 && slot->bitmap.width > 0)
						CM_EXCEPT(InternalErrorException, "Failed to render glyph bitmap");

					UINT8* sourceBuffer = slot->bitmap.buffer;
					UINT8* dstBuffer = pixelBuffer + (curElement.output.y * pageIter->width * 2) + curElement.output.x * 2;

					if(slot->bitmap.pixel_mode == ft_pixel_mode_grays)
					{
						for(INT32 bitmapRow = 0; bitmapRow < slot->bitmap.rows; bitmapRow++)
						{
							for(INT32 bitmapColumn = 0; bitmapColumn < slot->bitmap.width; bitmapColumn++)
							{
								dstBuffer[bitmapColumn * 2 + 0] = sourceBuffer[bitmapColumn];
								dstBuffer[bitmapColumn * 2 + 1] = sourceBuffer[bitmapColumn];
							}

							dstBuffer += pageIter->width * 2;
							sourceBuffer += slot->bitmap.pitch;
						}
					}
					else if(slot->bitmap.pixel_mode == ft_pixel_mode_mono)
					{
						// 8 pixels are packed into a byte, so do some unpacking
						for(INT32 bitmapRow = 0; bitmapRow < slot->bitmap.rows; bitmapRow++)
						{
							for(INT32 bitmapColumn = 0; bitmapColumn < slot->bitmap.width; bitmapColumn++)
							{
								UINT8 srcValue = sourceBuffer[bitmapColumn >> 3];
								UINT8 dstValue = (srcValue & (128 >> (bitmapColumn & 7))) != 0 ? 255 : 0;

								dstBuffer[bitmapColumn * 2 + 0] = dstValue;
								dstBuffer[bitmapColumn * 2 + 1] = dstValue;
							}

							dstBuffer += pageIter->width * 2;
							sourceBuffer += slot->bitmap.pitch;
						}
					}
					else
						CM_EXCEPT(InternalErrorException, "Unsupported pixel mode for a FreeType bitmap.");

					// Store character information
					CHAR_DESC charDesc;

					float invTexWidth = 1.0f / pageIter->width;
					float invTexHeight = 1.0f / pageIter->height;

					charDesc.charId = charIdx;
					charDesc.width = curElement.input.width;
					charDesc.height = curElement.input.height;
					charDesc.page = curElement.output.page;
					charDesc.uvWidth = invTexWidth * curElement.input.width;
					charDesc.uvHeight = invTexHeight * curElement.input.height;
					charDesc.uvX = invTexWidth * curElement.output.x;
					charDesc.uvY = invTexHeight * curElement.output.y;
					charDesc.xOffset = slot->bitmap_left;
					charDesc.yOffset = slot->bitmap_top;
					charDesc.xAdvance = slot->advance.x >> 6;
					charDesc.yAdvance = slot->advance.y >> 6;

					baselineOffset = std::max(baselineOffset, (INT32)(slot->metrics.horiBearingY >> 6));
					lineHeight = std::max(lineHeight, charDesc.height);

					// Load kerning and store char
					if(!isMissingGlypth)
					{
						FT_Vector resultKerning;
						for(auto kerningIter = charIndexRanges.begin(); kerningIter != charIndexRanges.end(); ++kerningIter)
						{
							for(UINT32 kerningCharIdx = kerningIter->first; kerningCharIdx <= kerningIter->second; kerningCharIdx++)
							{
								if(kerningCharIdx == charIdx)
									continue;

								error = FT_Get_Kerning(face, charIdx, kerningCharIdx, FT_KERNING_DEFAULT, &resultKerning);

								if(error)
									CM_EXCEPT(InternalErrorException, "Failed to get kerning information for character: " + toString(charIdx));

								INT32 kerningX = (INT32)(resultKerning.x >> 6); // Y kerning is ignored because it is so rare
								if(kerningX == 0) // We don't store 0 kerning, this is assumed default
									continue;

								KerningPair pair;
								pair.amount = kerningX;
								pair.otherCharId = kerningCharIdx;

								charDesc.kerningPairs.push_back(pair);
							}
						}

						fontData.fontDesc.characters[charIdx] = charDesc;
					}
					else
					{
						fontData.fontDesc.missingGlyph = charDesc;
					}
				}

				HTexture newTex = Texture::create(TEX_TYPE_2D, pageIter->width, pageIter->height, 0, PF_R8G8);
				newTex.waitUntilLoaded();

				UINT32 subresourceIdx = newTex->mapToSubresourceIdx(0, 0);
				gMainSyncedCA().writeSubresource(newTex.getInternalPtr(), subresourceIdx, pixelData);
				gMainSyncedCA().submitToCoreThread(true); // TODO - Possibly we can avoid this. I don't see a reason we need to wait for the update to complete.

				fontData.texturePages.push_back(newTex);

				pageIdx++;
			}

			fontData.size = fontSizes[i];
			fontData.fontDesc.baselineOffset = baselineOffset;
			fontData.fontDesc.lineHeight = lineHeight;

			// Get space size
			error = FT_Load_Char(face, 32, loadFlags);

			if(error)
				CM_EXCEPT(InternalErrorException, "Failed to load a character");

			fontData.fontDesc.spaceWidth = face->glyph->advance.x >> 6;

			dataPerSize.push_back(fontData);
		}

		HFont newFont = Font::create(dataPerSize);

		FT_Done_FreeType(library);

		return newFont;
	}
}