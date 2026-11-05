# Original implementations retained for differential tests during conversion.
foreach(unit IN ITEMS SkPixelRef SkMallocPixelRef SkPixelStorage SkIDChangeListener SkMatrix SkDevice
        SkAutoPixmapStorage SkSafeMath SkReadPixelsRec SkWritePixelsRec SkImageInfo SkConvertPixels SkPixmap SkPixmapDraw SkMask SkMaskBlurFilter SkGaussFilter SkMaskCache SkVertices SkVertState SkBlurMask SkCachedData SkMaskFilterBase SkMaskFilter SkBlurMaskFilterImpl SkCanvasPriv SkCanvas SkRecordCanvas SkPictureRecord SkPicturePlayback SkRecordDraw SkRecordOpts SkPaint SkPaintPriv SkM44 SkMatrixInvert SkCPUContext SkGraphics SkBitmapDevice SkBlitter_Sprite SkBlitter SkBlitter_A8 SkBlitter_ARGB32 SkRasterPipelineBlitter SkSpriteBlitter_ARGB32 SkDraw SkDraw_atlas SkDraw_vertices SkBlurEngine SkImageFilter SkImageFilterCache SkImageFilterTypes SkLocalMatrixImageFilter)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

list(REMOVE_ITEM stx_sources skia/src/core/SkArenaAlloc.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkArenaAlloc.cpp)

list(REMOVE_ITEM stx_sources skia/src/core/SkBlockAllocator.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkBlockAllocator.cpp)

foreach(unit IN ITEMS src/core/SkAAClip.cpp src/core/SkRasterClip.cpp
        src/core/SkRegion.cpp src/core/SkRegion_path.cpp src/shaders/SkShaderBase.cpp
        src/shaders/SkLocalMatrixShader.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

list(REMOVE_ITEM stx_sources skia/src/effects/SkShaderMaskFilterImpl.cpp)
list(APPEND stx_sources baseline/skia/src/effects/SkShaderMaskFilterImpl.cpp)

# Removed factory sources remain reference-only at the original pin.
file(GLOB stx_reference_image_filters CONFIGURE_DEPENDS RELATIVE "${stx_library_root}"
    "${stx_library_root}/baseline/skia/src/effects/imagefilters/*.cpp")
list(FILTER stx_sources EXCLUDE REGEX "^skia/src/effects/imagefilters/")
list(APPEND stx_sources ${stx_reference_image_filters})

foreach(unit IN ITEMS src/utils/SkPaintFilterCanvas.cpp src/image/SkImage_RasterFactories.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

# Original runtime/compiler and adapted CPU entry points.
foreach(unit IN ITEMS
    src/core/SkBitmapDevice.cpp
    src/core/SkCanvas.cpp
    src/core/SkCapabilities.cpp
    src/core/SkKnownRuntimeEffects.cpp
    src/core/SkMesh.cpp
    src/core/SkRecordCanvas.cpp
    src/core/SkRecordDraw.cpp
    src/core/SkRuntimeBlender.cpp
    src/core/SkRuntimeEffect.cpp
    src/effects/SkBlenders.cpp
    src/effects/colorfilters/SkRuntimeColorFilter.cpp
    src/image/SkSurface.cpp
    src/image/SkSurface_Base.cpp
    src/image/SkSurface_Null.cpp
    src/image/SkSurface_Raster.cpp
    src/ports/SkGlobalInitialization_default.cpp
    src/shaders/SkBlendShader.cpp
    src/shaders/SkGainmapShader.cpp
    src/shaders/SkRuntimeShader.cpp
    src/sksl/SkSLAnalysis.cpp
    src/sksl/SkSLBuiltinTypes.cpp
    src/sksl/SkSLCompiler.cpp
    src/sksl/SkSLConstantFolder.cpp
    src/sksl/SkSLContext.cpp
    src/sksl/SkSLErrorReporter.cpp
    src/sksl/SkSLInliner.cpp
    src/sksl/SkSLIntrinsicList.cpp
    src/sksl/SkSLLexer.cpp
    src/sksl/SkSLMangler.cpp
    src/sksl/SkSLModule.cpp
    src/sksl/SkSLModuleDataDefault.cpp
    src/sksl/SkSLModuleLoader.cpp
    src/sksl/SkSLOperator.cpp
    src/sksl/SkSLOutputStream.cpp
    src/sksl/SkSLParser.cpp
    src/sksl/SkSLPool.cpp
    src/sksl/SkSLPosition.cpp
    src/sksl/SkSLSampleUsage.cpp
    src/sksl/SkSLString.cpp
    src/sksl/SkSLUtil.cpp
    src/sksl/analysis/SkSLCanExitWithoutReturningValue.cpp
    src/sksl/analysis/SkSLCheckProgramStructure.cpp
    src/sksl/analysis/SkSLCheckSymbolTableCorrectness.cpp
    src/sksl/analysis/SkSLFinalizationChecks.cpp
    src/sksl/analysis/SkSLGetLoopControlFlowInfo.cpp
    src/sksl/analysis/SkSLGetLoopUnrollInfo.cpp
    src/sksl/analysis/SkSLGetReturnComplexity.cpp
    src/sksl/analysis/SkSLHasSideEffects.cpp
    src/sksl/analysis/SkSLIsConstantExpression.cpp
    src/sksl/analysis/SkSLIsDynamicallyUniformExpression.cpp
    src/sksl/analysis/SkSLIsSameExpressionTree.cpp
    src/sksl/analysis/SkSLIsTrivialExpression.cpp
    src/sksl/analysis/SkSLProgramUsage.cpp
    src/sksl/analysis/SkSLReturnsInputAlpha.cpp
    src/sksl/analysis/SkSLSpecialization.cpp
    src/sksl/analysis/SkSLSwitchCaseContainsExit.cpp
    src/sksl/analysis/SkSLSymbolTableStackBuilder.cpp
    src/sksl/codegen/SkSLRasterPipelineBuilder.cpp
    src/sksl/codegen/SkSLRasterPipelineCodeGenerator.cpp
    src/sksl/ir/SkSLBinaryExpression.cpp
    src/sksl/ir/SkSLBlock.cpp
    src/sksl/ir/SkSLChildCall.cpp
    src/sksl/ir/SkSLConstructor.cpp
    src/sksl/ir/SkSLConstructorArray.cpp
    src/sksl/ir/SkSLConstructorArrayCast.cpp
    src/sksl/ir/SkSLConstructorCompound.cpp
    src/sksl/ir/SkSLConstructorCompoundCast.cpp
    src/sksl/ir/SkSLConstructorDiagonalMatrix.cpp
    src/sksl/ir/SkSLConstructorMatrixResize.cpp
    src/sksl/ir/SkSLConstructorScalarCast.cpp
    src/sksl/ir/SkSLConstructorSplat.cpp
    src/sksl/ir/SkSLConstructorStruct.cpp
    src/sksl/ir/SkSLDiscardStatement.cpp
    src/sksl/ir/SkSLDoStatement.cpp
    src/sksl/ir/SkSLExpression.cpp
    src/sksl/ir/SkSLExpressionStatement.cpp
    src/sksl/ir/SkSLExtension.cpp
    src/sksl/ir/SkSLFieldAccess.cpp
    src/sksl/ir/SkSLForStatement.cpp
    src/sksl/ir/SkSLFunctionCall.cpp
    src/sksl/ir/SkSLFunctionDeclaration.cpp
    src/sksl/ir/SkSLFunctionDefinition.cpp
    src/sksl/ir/SkSLIfStatement.cpp
    src/sksl/ir/SkSLIndexExpression.cpp
    src/sksl/ir/SkSLInterfaceBlock.cpp
    src/sksl/ir/SkSLLayout.cpp
    src/sksl/ir/SkSLLiteral.cpp
    src/sksl/ir/SkSLModifierFlags.cpp
    src/sksl/ir/SkSLModifiersDeclaration.cpp
    src/sksl/ir/SkSLPostfixExpression.cpp
    src/sksl/ir/SkSLPrefixExpression.cpp
    src/sksl/ir/SkSLProgram.cpp
    src/sksl/ir/SkSLSetting.cpp
    src/sksl/ir/SkSLStructDefinition.cpp
    src/sksl/ir/SkSLSwitchCase.cpp
    src/sksl/ir/SkSLSwitchStatement.cpp
    src/sksl/ir/SkSLSwizzle.cpp
    src/sksl/ir/SkSLSymbol.cpp
    src/sksl/ir/SkSLSymbolTable.cpp
    src/sksl/ir/SkSLTernaryExpression.cpp
    src/sksl/ir/SkSLType.cpp
    src/sksl/ir/SkSLTypeReference.cpp
    src/sksl/ir/SkSLVarDeclarations.cpp
    src/sksl/ir/SkSLVariable.cpp
    src/sksl/ir/SkSLVariableReference.cpp
    src/sksl/tracing/SkSLDebugTracePlayer.cpp
    src/sksl/tracing/SkSLDebugTracePriv.cpp
    src/sksl/tracing/SkSLTraceHook.cpp
    src/sksl/transform/SkSLAddConstToVarModifiers.cpp
    src/sksl/transform/SkSLEliminateDeadFunctions.cpp
    src/sksl/transform/SkSLEliminateDeadGlobalVariables.cpp
    src/sksl/transform/SkSLEliminateDeadLocalVariables.cpp
    src/sksl/transform/SkSLEliminateEmptyStatements.cpp
    src/sksl/transform/SkSLEliminateUnnecessaryBraces.cpp
    src/sksl/transform/SkSLEliminateUnreachableCode.cpp
    src/sksl/transform/SkSLFindAndDeclareBuiltinFunctions.cpp
    src/sksl/transform/SkSLFindAndDeclareBuiltinStructs.cpp
    src/sksl/transform/SkSLFindAndDeclareBuiltinVariables.cpp
    src/sksl/transform/SkSLHoistSwitchVarDeclarationsAtTopLevel.cpp
    src/sksl/transform/SkSLRenamePrivateSymbols.cpp
    src/sksl/transform/SkSLReplaceConstVarsWithLiterals.cpp
    src/sksl/transform/SkSLReplaceSplatCastsWithSwizzles.cpp
    src/sksl/transform/SkSLRewriteIndexedSwizzle.cpp
    src/sksl/transform/SkSLTransform.cpp
    src/utils/SkShaderUtils.cpp
)
    list(REMOVE_ITEM stx_sources skia/${unit} baseline/skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

foreach(unit IN ITEMS src/core/SkReadBuffer.cpp src/core/SkGlyph.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

list(REMOVE_ITEM stx_sources skia/src/core/SkRasterPipeline.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkRasterPipeline.cpp)

list(REMOVE_ITEM stx_sources skia/src/core/SkOpts.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkOpts.cpp)

# Original text drawing and adapted canvas/device forwarding.
foreach(unit IN ITEMS
    src/capture/SkCaptureCanvas.cpp
    src/core/SkBitmapDevice.cpp
    src/core/SkCanvas.cpp
    src/core/SkDevice.cpp
    src/core/SkDraw_text.cpp
    src/core/SkGlyphRunPainter.cpp
    src/core/SkOverdrawCanvas.cpp
    src/core/SkPicturePlayback.cpp
    src/core/SkPictureRecord.cpp
    src/core/SkRecordCanvas.cpp
    src/core/SkRecordDraw.cpp
    src/text/GlyphRun.cpp
    src/utils/SkNWayCanvas.cpp
    src/utils/SkPaintFilterCanvas.cpp
    src/utils/SkTextUtils.cpp
)
    list(REMOVE_ITEM stx_sources skia/${unit} baseline/skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

list(REMOVE_ITEM stx_sources skia/src/core/SkCanvas_Raster.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkCanvas_Raster.cpp)

list(REMOVE_ITEM stx_sources skia/src/core/SkTextBlob.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkTextBlob.cpp)

# Original picture/capture implementations and adapted entry points.
foreach(unit IN ITEMS
    src/capture/SkCapture.cpp
    src/capture/SkCaptureCanvas.cpp
    src/capture/SkCaptureManager.cpp
    src/core/SkBBHFactory.cpp
    src/core/SkBigPicture.cpp
    src/core/SkCPURecorder.cpp
    src/core/SkCanvas.cpp
    src/core/SkDrawable.cpp
    src/core/SkPicture.cpp
    src/core/SkPictureData.cpp
    src/core/SkPictureFlat.cpp
    src/core/SkPicturePlayback.cpp
    src/core/SkPictureRecord.cpp
    src/core/SkPictureRecorder.cpp
    src/core/SkRTree.cpp
    src/core/SkRecord.cpp
    src/core/SkRecordCanvas.cpp
    src/core/SkRecordDraw.cpp
    src/core/SkRecordOpts.cpp
    src/core/SkRecordedDrawable.cpp
    src/core/SkRecords.cpp
    src/image/SkImage_LazyFactories.cpp
    src/image/SkImage_Picture.cpp
    src/image/SkPictureImageGenerator.cpp
    src/image/SkSurface_Base.cpp
    src/shaders/SkPictureShader.cpp
    src/utils/SkMultiPictureDocument.cpp
    src/utils/SkNWayCanvas.cpp
    src/utils/SkPaintFilterCanvas.cpp
)
    list(REMOVE_ITEM stx_sources skia/${unit} baseline/skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

# Original font closure and adapted resource/serialization boundaries.
foreach(unit IN ITEMS
    src/core/SkDescriptor.cpp
    src/core/SkFont.cpp
    src/core/SkFontDescriptor.cpp
    src/core/SkFontMetricsPriv.cpp
    src/core/SkFontMgr.cpp
    src/core/SkFontStream.cpp
    src/core/SkFont_serial.cpp
    src/core/SkGlyph.cpp
    src/core/SkGraphics.cpp
    src/core/SkReadBuffer.cpp
    src/core/SkScalerContext.cpp
    src/core/SkStrike.cpp
    src/core/SkStrikeCache.cpp
    src/core/SkStrikeRef.cpp
    src/core/SkStrikeSpec.cpp
    src/core/SkTextBlob.cpp
    src/core/SkTypeface.cpp
    src/core/SkTypefaceCache.cpp
    src/core/SkTypeface_remote.cpp
    src/core/SkWriteBuffer.cpp
    src/text/SlugFromBuffer.cpp
    src/text/StrikeForGPU.cpp
    src/utils/SkCharToGlyphCache.cpp
    src/utils/SkCustomTypeface.cpp
    src/utils/SkOrderedFontMgr.cpp
    src/utils/mac/SkCTFont.cpp
    src/utils/mac/SkCTFontCreateExactCopy.cpp
    src/utils/win/SkDWriteFontFileStream.cpp
)
    list(REMOVE_ITEM stx_sources skia/${unit} baseline/skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

list(REMOVE_ITEM stx_sources skia/src/utils/win/SkDWrite.cpp)
list(APPEND stx_sources baseline/skia/src/utils/win/SkDWrite.cpp)

list(REMOVE_ITEM stx_sources skia/src/core/SkMaskGamma.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkMaskGamma.cpp)

# Removed from the candidate on 2026-10-27; the reference keeps compiling the originals.
foreach(unit IN ITEMS src/core/SkDistanceFieldGen.cpp src/core/SkDocument.cpp
        src/utils/SkCamera.cpp src/utils/SkJSONWriter.cpp src/utils/SkParse.cpp
        src/utils/SkParseColor.cpp src/utils/SkParsePath.cpp src/utils/SkFloatToDecimal.cpp
        src/utils/SkOSPath.cpp src/utils/mac/SkCreateCGImageRef.cpp src/utils/SkPatchUtils.cpp
        src/utils/SkMatrix22.cpp src/utils/win/SkAutoCoInitialize.cpp
        src/utils/win/SkDWriteGeometrySink.cpp src/utils/win/SkHRESULT.cpp
        src/utils/win/SkIStream.cpp src/core/SkStream.cpp src/core/SkMD5.cpp
        src/core/SkATrace.cpp src/utils/SkEventTracer.cpp src/core/SkSLTypeShared.cpp
        src/core/SkSpinlock.cpp src/core/SkMasks.cpp src/core/SkMathPriv.cpp src/core/SkUtils.cpp
        src/core/SkBitmapProcState.cpp src/core/SkBitmapProcState_matrixProcs.cpp
        src/core/SkBitmapProcState_opts.cpp src/core/SkBitmapProcState_opts_lasx.cpp
        src/core/SkBitmapProcState_opts_ssse3.cpp src/shaders/SkBitmapProcShader.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

# Strings and the OS file ports before the string prune. The port is the POSIX or the
# Windows file, whichever this platform compiles.
foreach(unit IN ITEMS src/core/SkString.cpp src/core/SkStringUtils.cpp src/core/SkUTF.cpp
                      src/core/SkBuffer.cpp src/core/SkDataTable.cpp src/core/SkFloatingPoint.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()
list(TRANSFORM stx_sources REPLACE "^skia/src/ports/SkOSFile_(posix|win)\\.cpp$"
    "baseline/skia/src/ports/SkOSFile_\\1.cpp")

if(NOT APPLE)
    list(FILTER stx_sources EXCLUDE REGEX "baseline/skia/src/utils/mac/.*\\.cpp$")
endif()

list(REMOVE_ITEM stx_sources skia/src/sfnt/SkOTUtils.cpp)
list(APPEND stx_sources baseline/skia/src/sfnt/SkOTUtils.cpp)

list(REMOVE_ITEM stx_sources skia/src/sfnt/SkOTTable_name.cpp)
list(APPEND stx_sources baseline/skia/src/sfnt/SkOTTable_name.cpp)

list(REMOVE_ITEM stx_sources skia/src/image/SkTiledImageUtils.cpp)
list(APPEND stx_sources baseline/skia/src/image/SkTiledImageUtils.cpp)

# Original image ownership and raster implementation for differential checks.
foreach(unit IN ITEMS SkImage_Base SkImage_Raster SkImage_Lazy)
    list(REMOVE_ITEM stx_sources skia/src/image/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/image/${unit}.cpp)
endforeach()

list(APPEND stx_sources baseline/skia/src/core/SkCompressedDataUtils.cpp)

list(REMOVE_ITEM stx_sources skia/src/core/SkImageGenerator.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkImageGenerator.cpp
    baseline/skia/src/core/SkYUVPlanesCache.cpp)

list(APPEND stx_sources baseline/skia/src/core/SkYUVAInfo.cpp
    baseline/skia/src/core/SkYUVAPixmaps.cpp)

foreach(unit IN ITEMS src/image/SkImage.cpp src/core/SkMipmapAccessor.cpp src/core/SkSpecialImage.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

list(REMOVE_ITEM stx_sources skia/src/core/SkBitmapCache.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkBitmapCache.cpp)

foreach(unit IN ITEMS SkMipmap SkMipmapBuilder)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

list(REMOVE_ITEM stx_sources skia/src/core/SkMipmapHQDownSampler.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkMipmapHQDownSampler.cpp)

list(REMOVE_ITEM stx_sources skia/src/core/SkMipmapDrawDownSampler.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkMipmapDrawDownSampler.cpp)

foreach(unit IN ITEMS SkResourceCache SkSynchronizedResourceCache)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

# Removed unused task scheduler remains available only to the reference build.
list(APPEND stx_sources
    baseline/skia/src/core/SkExecutor.cpp
    baseline/skia/src/core/SkTaskGroup.cpp)

foreach(unit IN ITEMS SkSemaphore SkThreadID SkSharedMutex)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

foreach(unit IN ITEMS src/core/SkTDArray.cpp src/utils/SkPolyUtils.cpp src/utils/SkShadowUtils.cpp
        src/utils/SkShadowTessellator.cpp src/core/SkDrawShadowInfo.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

foreach(unit IN ITEMS src/lazy/SkDiscardableMemoryPool.cpp src/ports/SkDiscardableMemory_none.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

# Removed picture factory bookkeeping and its search utilities remain reference-only.
foreach(unit IN ITEMS SkPtrRecorder SkTSearch SkFlattenable)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

# Original effect registration remains confined to the reference build.
foreach(unit IN ITEMS
    src/core/SkBlurMaskFilterImpl.cpp
    src/core/SkGlobalInitialization_core.cpp
    src/core/SkMaskFilter.cpp
    src/core/SkPathEffect.cpp
    src/effects/Sk1DPathEffect.cpp
    src/effects/Sk2DPathEffect.cpp
    src/effects/SkCornerPathEffect.cpp
    src/effects/SkDiscretePathEffect.cpp
    src/effects/SkShaderMaskFilterImpl.cpp
    src/effects/SkTableMaskFilter.cpp
    src/effects/colorfilters/SkBlendModeColorFilter.cpp
    src/effects/colorfilters/SkColorSpaceXformColorFilter.cpp
    src/effects/colorfilters/SkComposeColorFilter.cpp
    src/effects/colorfilters/SkMatrixColorFilter.cpp
    src/effects/colorfilters/SkTableColorFilter.cpp
    src/effects/colorfilters/SkWorkingFormatColorFilter.cpp
    src/ports/SkGlobalInitialization_default.cpp
    src/shaders/SkBlendShader.cpp
    src/shaders/SkColorShader.cpp
    src/shaders/SkCoordClampShader.cpp
    src/shaders/SkEmptyShader.cpp
    src/shaders/SkImageShader.cpp
    src/shaders/SkPerlinNoiseShaderImpl.cpp
    src/shaders/SkWorkingColorSpaceShader.cpp
    src/shaders/gradients/SkConicalGradient.cpp
    src/shaders/gradients/SkLinearGradient.cpp
    src/shaders/gradients/SkRadialGradient.cpp
    src/shaders/gradients/SkSweepGradient.cpp
)
    list(REMOVE_ITEM stx_sources skia/${unit} baseline/skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

# Preserve the original storage writer with the original serialization profile.
list(REMOVE_ITEM stx_sources skia/src/core/SkWriter32.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkWriter32.cpp)

# Original allocator policy and product wrappers remain differential references.
foreach(unit IN ITEMS src/core/SkMalloc.cpp src/ports/SkMemory_malloc.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

# Preserve original container capacity and allocation-span behavior.
list(REMOVE_ITEM stx_sources skia/src/core/SkContainers.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkContainers.cpp)

# Original block deque retained for differential verification.
list(REMOVE_ITEM stx_sources skia/src/core/SkDeque.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkDeque.cpp)

# Bitmap ownership and facade conversion has an independent pre-conversion reference.
list(REMOVE_ITEM stx_sources skia/src/core/SkBitmap.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkBitmap.cpp)

# Matrix and lookup-table value storage retain their pre-conversion implementations.
foreach(unit IN ITEMS src/core/SkColorTable.cpp src/effects/SkColorMatrix.cpp
                      src/effects/SkColorMatrixFilter.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

# Color-filter facade and scalar pipeline keep the original conversion path.
foreach(unit IN ITEMS src/core/SkColorFilter.cpp src/effects/colorfilters/SkColorFilterBase.cpp
                      src/shaders/SkColorFilterShader.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

# Preserve the original color-space plan and Gaussian coverage filter.
foreach(unit IN ITEMS src/core/SkColorSpaceXformSteps.cpp
                      src/effects/colorfilters/SkGaussianColorFilter.cpp)
    list(REMOVE_ITEM stx_sources skia/${unit})
    list(APPEND stx_sources baseline/skia/${unit})
endforeach()

# Shader facade retains its pre-conversion ownership and matrix composition.
list(REMOVE_ITEM stx_sources skia/src/shaders/SkShader.cpp)
list(APPEND stx_sources baseline/skia/src/shaders/SkShader.cpp)

# Mutable shader transforms retain the original child-failure behavior.
list(REMOVE_ITEM stx_sources skia/src/shaders/SkTransformShader.cpp)
list(APPEND stx_sources baseline/skia/src/shaders/SkTransformShader.cpp)

# Original gradient storage, interpolation and stage construction.
list(REMOVE_ITEM stx_sources skia/src/shaders/gradients/SkGradientBaseShader.cpp)
list(APPEND stx_sources baseline/skia/src/shaders/gradients/SkGradientBaseShader.cpp)

# Original point, rectangle and rounded-rectangle implementations.
list(REMOVE_ITEM stx_sources skia/src/core/SkPoint.cpp skia/src/core/SkRect.cpp skia/src/core/SkRRect.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkPoint.cpp baseline/skia/src/core/SkRect.cpp
    baseline/skia/src/core/SkRRect.cpp)

# Path storage, construction, iteration and serialization before conversion. The
# builder copy includes the two earlier aliasing corrections (c420bc8, 0d1ed74).
foreach(unit IN ITEMS SkPath SkPathBuilder SkPathPriv SkPathData SkPathRaw SkPathIter SkPath_serial
        SkPathRawShapes SkPath_interpolate SkPathDump)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

# Curve evaluation, chopping, root finding, cubic clipping and cubic maps before conversion.
foreach(unit IN ITEMS SkGeometry SkBezierCurves SkQuads SkCubics SkCubicClipper SkCubicMap)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

# Contour measurement and the path-measure facade before conversion.
list(REMOVE_ITEM stx_sources skia/src/core/SkContourMeasure.cpp skia/src/core/SkPathMeasure.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkContourMeasure.cpp
    baseline/skia/src/core/SkPathMeasure.cpp)

# Stroking, caps and joins, and stroke records before conversion.
list(REMOVE_ITEM stx_sources skia/src/core/SkStroke.cpp skia/src/core/SkStrokerPriv.cpp
    skia/src/core/SkStrokeRec.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkStroke.cpp baseline/skia/src/core/SkStrokerPriv.cpp
    baseline/skia/src/core/SkStrokeRec.cpp)

# Dashing and the dash path effect before conversion (SkPathEffect.cpp is selected above).
list(REMOVE_ITEM stx_sources skia/src/utils/SkDashPath.cpp skia/src/effects/SkDashPathEffect.cpp)
list(APPEND stx_sources baseline/skia/src/utils/SkDashPath.cpp
    baseline/skia/src/effects/SkDashPathEffect.cpp)

# Line and edge clipping before conversion, and the unreferenced quad clipper that the
# candidate no longer carries.
list(REMOVE_ITEM stx_sources skia/src/core/SkLineClipper.cpp skia/src/core/SkEdgeClipper.cpp
    skia/src/core/SkQuadClipper.cpp)
list(APPEND stx_sources baseline/skia/src/core/SkLineClipper.cpp
    baseline/skia/src/core/SkEdgeClipper.cpp baseline/skia/src/core/SkQuadClipper.cpp)

# Scan-conversion edges and the edge builder before conversion.
foreach(unit IN ITEMS SkEdge SkAnalyticEdge SkEdgeBuilder)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

# Scan entry points, the non-anti-aliased path filler and hairlines before conversion.
foreach(unit IN ITEMS SkScan SkScan_Path SkScan_Hairline)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

# Anti-aliased hairlines, rectangles and path fills before conversion.
foreach(unit IN ITEMS SkScan_Antihair SkScan_AntiPath SkScan_AAAPath)
    list(REMOVE_ITEM stx_sources skia/src/core/${unit}.cpp)
    list(APPEND stx_sources baseline/skia/src/core/${unit}.cpp)
endforeach()

# Upstream directories the candidate never compiled moved to baseline/skia on 2026-10-27; the
# reference keeps compiling its sources from them there.
list(TRANSFORM stx_sources REPLACE
    "^skia/src/(gpu|codec|pathops|pdf|encode|partition_alloc|xml|xps|svg|android|capture)/"
    "baseline/skia/src/\\1/")
