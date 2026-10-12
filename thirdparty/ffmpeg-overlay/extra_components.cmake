# dekp23 custom music: extra FFmpeg components so the game can convert your tracks
# (mp3/aac/m4a/opus/webm/ogg/flac/alac/wav/aiff) in-process. Included from
# thirdparty/CMakeLists.txt after libavutil/libavcodec are defined. The FFmpeg submodule is
# not modified: an overlay config.h includes the original platform config and enables the
# extra components (config_overrides.h); generated list files come from the overlay.
# Lists derived from FFmpeg 4.4 configure:
#   base: the SDK's FFMPEG_CONFIGURATION (+ --disable-inline-asm)
#   ext:  base + --enable-swresample --enable-decoder=aac,aac_latm,opus,vorbis,flac,alac,
#         pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_f64le,pcm_u8,
#         pcm_alaw,pcm_mulaw,mp2,mp2float,wmav1 --enable-demuxer=matroska,mov,ogg,flac,wav,
#         aiff,aac,w64 --enable-parser=aac,flac,opus,vorbis

set(FFMPEG_EXTRA_OVERLAY_SRC "${CMAKE_CURRENT_LIST_DIR}")
file(MAKE_DIRECTORY "${FFMPEG_OVERLAY_BUILD_DIR}/libavformat")
file(WRITE "${FFMPEG_OVERLAY_BUILD_DIR}/config.h.in"
  "/* generated: original platform config + extra components */\n"
  "#include \"${FFMPEG_DIR}/config.h\"\n"
  "#include \"${FFMPEG_EXTRA_OVERLAY_SRC}/config_overrides.h\"\n")
configure_file("${FFMPEG_OVERLAY_BUILD_DIR}/config.h.in" "${FFMPEG_OVERLAY_BUILD_DIR}/config.h" COPYONLY)
foreach(_f demuxer_list.c muxer_list.c protocol_list.c)
  configure_file("${FFMPEG_EXTRA_OVERLAY_SRC}/${_f}" "${FFMPEG_OVERLAY_BUILD_DIR}/libavformat/${_f}" COPYONLY)
endforeach()

target_include_directories(libavutil BEFORE PRIVATE "${FFMPEG_OVERLAY_BUILD_DIR}")
target_sources(libavutil PRIVATE
    ${FFMPEG_DIR}/libavutil/lzo.c
)

target_sources(libavcodec PRIVATE
    ${FFMPEG_DIR}/libavcodec/aac_ac3_parser.c
    ${FFMPEG_DIR}/libavcodec/aac_parser.c
    ${FFMPEG_DIR}/libavcodec/aacdec.c
    ${FFMPEG_DIR}/libavcodec/aacps_common.c
    ${FFMPEG_DIR}/libavcodec/aacps_float.c
    ${FFMPEG_DIR}/libavcodec/aacpsdsp_float.c
    ${FFMPEG_DIR}/libavcodec/aacsbr.c
    ${FFMPEG_DIR}/libavcodec/ac3tab.c
    ${FFMPEG_DIR}/libavcodec/adts_header.c
    ${FFMPEG_DIR}/libavcodec/alac.c
    ${FFMPEG_DIR}/libavcodec/alac_data.c
    ${FFMPEG_DIR}/libavcodec/alacdsp.c
    ${FFMPEG_DIR}/libavcodec/cbrt_data.c
    ${FFMPEG_DIR}/libavcodec/flac.c
    ${FFMPEG_DIR}/libavcodec/flac_parser.c
    ${FFMPEG_DIR}/libavcodec/flacdata.c
    ${FFMPEG_DIR}/libavcodec/flacdec.c
    ${FFMPEG_DIR}/libavcodec/flacdsp.c
    ${FFMPEG_DIR}/libavcodec/golomb.c
    ${FFMPEG_DIR}/libavcodec/kbdwin.c
    ${FFMPEG_DIR}/libavcodec/latm_parser.c
    ${FFMPEG_DIR}/libavcodec/mdct15.c
    ${FFMPEG_DIR}/libavcodec/mpeg4audio.c
    ${FFMPEG_DIR}/libavcodec/opus.c
    ${FFMPEG_DIR}/libavcodec/opus_celt.c
    ${FFMPEG_DIR}/libavcodec/opus_parser.c
    ${FFMPEG_DIR}/libavcodec/opus_pvq.c
    ${FFMPEG_DIR}/libavcodec/opus_rc.c
    ${FFMPEG_DIR}/libavcodec/opus_silk.c
    ${FFMPEG_DIR}/libavcodec/opusdec.c
    ${FFMPEG_DIR}/libavcodec/opusdsp.c
    ${FFMPEG_DIR}/libavcodec/opustab.c
    ${FFMPEG_DIR}/libavcodec/pcm.c
    ${FFMPEG_DIR}/libavcodec/sbrdsp.c
    ${FFMPEG_DIR}/libavcodec/vorbis.c
    ${FFMPEG_DIR}/libavcodec/vorbis_data.c
    ${FFMPEG_DIR}/libavcodec/vorbisdec.c
    ${FFMPEG_DIR}/libavcodec/vorbisdsp.c
)
if(IS_X86_64)
  target_sources(libavcodec PRIVATE
    ${FFMPEG_DIR}/libavcodec/x86/aacpsdsp_init.c
    ${FFMPEG_DIR}/libavcodec/x86/alacdsp_init.c
    ${FFMPEG_DIR}/libavcodec/x86/flacdsp_init.c
    ${FFMPEG_DIR}/libavcodec/x86/mdct15_init.c
    ${FFMPEG_DIR}/libavcodec/x86/opusdsp_init.c
    ${FFMPEG_DIR}/libavcodec/x86/sbrdsp_init.c
    ${FFMPEG_DIR}/libavcodec/x86/vorbisdsp_init.c
  )
endif()
if(IS_ARM64)
  target_sources(libavcodec PRIVATE
    ${FFMPEG_DIR}/libavcodec/aarch64/aacpsdsp_init_aarch64.c
    ${FFMPEG_DIR}/libavcodec/aarch64/sbrdsp_init_aarch64.c
    ${FFMPEG_DIR}/libavcodec/aarch64/opusdsp_init.c
    ${FFMPEG_DIR}/libavcodec/aarch64/vorbisdsp_init.c
    ${FFMPEG_DIR}/libavcodec/aarch64/sbrdsp_neon.S
    ${FFMPEG_DIR}/libavcodec/aarch64/aacpsdsp_neon.S
    ${FFMPEG_DIR}/libavcodec/aarch64/opusdsp_neon.S
    ${FFMPEG_DIR}/libavcodec/aarch64/vorbisdsp_neon.S
  )
endif()

# libswresample - sample format / rate conversion
add_library(libswresample STATIC
    ${FFMPEG_DIR}/libswresample/audioconvert.c
    ${FFMPEG_DIR}/libswresample/dither.c
    ${FFMPEG_DIR}/libswresample/options.c
    ${FFMPEG_DIR}/libswresample/rematrix.c
    ${FFMPEG_DIR}/libswresample/resample.c
    ${FFMPEG_DIR}/libswresample/resample_dsp.c
    ${FFMPEG_DIR}/libswresample/swresample.c
    ${FFMPEG_DIR}/libswresample/swresample_frame.c
)
if(IS_X86_64)
  target_sources(libswresample PRIVATE
    ${FFMPEG_DIR}/libswresample/x86/audio_convert_init.c
    ${FFMPEG_DIR}/libswresample/x86/rematrix_init.c
    ${FFMPEG_DIR}/libswresample/x86/resample_init.c
  )
endif()
if(IS_ARM64)
  target_sources(libswresample PRIVATE
    ${FFMPEG_DIR}/libswresample/aarch64/audio_convert_init.c
    ${FFMPEG_DIR}/libswresample/aarch64/resample_init.c
    ${FFMPEG_DIR}/libswresample/aarch64/audio_convert_neon.S
    ${FFMPEG_DIR}/libswresample/aarch64/resample.S
  )
  target_compile_definitions(libswresample PRIVATE HAVE_NEON=1 HAVE_ARMV8=1 HAVE_INLINE_ASM=1)
endif()

# libavformat - demuxers (file protocol only, no network)
add_library(libavformat STATIC
    ${FFMPEG_DIR}/libavformat/aacdec.c
    ${FFMPEG_DIR}/libavformat/aiff.c
    ${FFMPEG_DIR}/libavformat/aiffdec.c
    ${FFMPEG_DIR}/libavformat/allformats.c
    ${FFMPEG_DIR}/libavformat/apetag.c
    ${FFMPEG_DIR}/libavformat/asf.c
    ${FFMPEG_DIR}/libavformat/asfcrypt.c
    ${FFMPEG_DIR}/libavformat/asfdec_f.c
    ${FFMPEG_DIR}/libavformat/avio.c
    ${FFMPEG_DIR}/libavformat/aviobuf.c
    ${FFMPEG_DIR}/libavformat/avlanguage.c
    ${FFMPEG_DIR}/libavformat/dump.c
    ${FFMPEG_DIR}/libavformat/file.c
    ${FFMPEG_DIR}/libavformat/flac_picture.c
    ${FFMPEG_DIR}/libavformat/flacdec.c
    ${FFMPEG_DIR}/libavformat/format.c
    ${FFMPEG_DIR}/libavformat/id3v1.c
    ${FFMPEG_DIR}/libavformat/id3v2.c
    ${FFMPEG_DIR}/libavformat/img2.c
    ${FFMPEG_DIR}/libavformat/isom.c
    ${FFMPEG_DIR}/libavformat/isom_tags.c
    ${FFMPEG_DIR}/libavformat/matroska.c
    ${FFMPEG_DIR}/libavformat/matroskadec.c
    ${FFMPEG_DIR}/libavformat/metadata.c
    ${FFMPEG_DIR}/libavformat/mov.c
    ${FFMPEG_DIR}/libavformat/mov_chan.c
    ${FFMPEG_DIR}/libavformat/mov_esds.c
    ${FFMPEG_DIR}/libavformat/mp3dec.c
    ${FFMPEG_DIR}/libavformat/mux.c
    ${FFMPEG_DIR}/libavformat/oggdec.c
    ${FFMPEG_DIR}/libavformat/oggparsecelt.c
    ${FFMPEG_DIR}/libavformat/oggparsedirac.c
    ${FFMPEG_DIR}/libavformat/oggparseflac.c
    ${FFMPEG_DIR}/libavformat/oggparseogm.c
    ${FFMPEG_DIR}/libavformat/oggparseopus.c
    ${FFMPEG_DIR}/libavformat/oggparseskeleton.c
    ${FFMPEG_DIR}/libavformat/oggparsespeex.c
    ${FFMPEG_DIR}/libavformat/oggparsetheora.c
    ${FFMPEG_DIR}/libavformat/oggparsevorbis.c
    ${FFMPEG_DIR}/libavformat/oggparsevp8.c
    ${FFMPEG_DIR}/libavformat/options.c
    ${FFMPEG_DIR}/libavformat/os_support.c
    ${FFMPEG_DIR}/libavformat/pcm.c
    ${FFMPEG_DIR}/libavformat/protocols.c
    ${FFMPEG_DIR}/libavformat/qtpalette.c
    ${FFMPEG_DIR}/libavformat/rawdec.c
    ${FFMPEG_DIR}/libavformat/replaygain.c
    ${FFMPEG_DIR}/libavformat/riff.c
    ${FFMPEG_DIR}/libavformat/riffdec.c
    ${FFMPEG_DIR}/libavformat/rmsipr.c
    ${FFMPEG_DIR}/libavformat/sdp.c
    ${FFMPEG_DIR}/libavformat/url.c
    ${FFMPEG_DIR}/libavformat/utils.c
    ${FFMPEG_DIR}/libavformat/vorbiscomment.c
    ${FFMPEG_DIR}/libavformat/w64.c
    ${FFMPEG_DIR}/libavformat/wavdec.c
)

foreach(_t libswresample libavformat)
  target_include_directories(${_t} BEFORE PRIVATE "${FFMPEG_OVERLAY_BUILD_DIR}")
  target_include_directories(${_t}
      PUBLIC $<BUILD_INTERFACE:${FFMPEG_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
      PRIVATE ${FFMPEG_ATOMICS_INCLUDE})
  target_compile_definitions(${_t} PRIVATE ${FFMPEG_COMMON_DEFINES})
  target_compile_options(${_t} PRIVATE -w)
  set_target_properties(${_t} PROPERTIES FOLDER "FFmpeg")
endforeach()
target_link_libraries(libswresample PUBLIC libavutil)
target_link_libraries(libavformat PUBLIC libavcodec libavutil)
if(WIN32)
  target_link_libraries(libavformat PRIVATE ws2_32 bcrypt)
endif()
