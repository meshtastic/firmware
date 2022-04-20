/*
 * Copyright (C) 2020 Siara Logics (cc)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @author Arundale Ramanathan
 *
 */

/**
 * @file unishox2.h
 * @author Arundale Ramanathan, James Z. M. Gao
 * @brief API for Unishox2 Compression and Decompression
 *
 * This file describes each function of the Unishox2 API \n
 * For finding out how this API can be used in your program, \n
 * please see test_unishox2.c.
 */

#ifndef unishox2
#define unishox2

#define UNISHOX_VERSION "2.0"   ///< Unicode spec version

/**
 * Macro switch to enable/disable output buffer length parameter in low level api \n
 * Disabled by default \n
 * When this macro is defined, the all the API functions \n
 * except the simple API functions accept an additional parameter olen \n
 * that enables the developer to pass the size of the output buffer provided \n
 * so that the api function may not write beyond that length. \n
 * This can be disabled if the developer knows that the buffer provided is sufficient enough \n
 * so no additional parameter is passed and the program is faster since additional check \n
 * for output length is not performed at each step \n
 * The simple api, i.e. unishox2_(de)compress_simple will always omit the buffer length
 */
#ifndef UNISHOX_API_WITH_OUTPUT_LEN
#  define UNISHOX_API_WITH_OUTPUT_LEN 0
#endif

/// Upto 8 bits of initial magic bit sequence can be included. Bit count can be specified with UNISHOX_MAGIC_BIT_LEN
#ifndef UNISHOX_MAGIC_BITS
#  define UNISHOX_MAGIC_BITS 0xFF
#endif

/// Desired length of Magic bits defined by UNISHOX_MAGIC_BITS
#ifdef UNISHOX_MAGIC_BIT_LEN
#  if UNISHOX_MAGIC_BIT_LEN < 0 || 9 <= UNISHOX_MAGIC_BIT_LEN
#    error "UNISHOX_MAGIC_BIT_LEN need between [0, 8)"
#  endif
#else
#  define UNISHOX_MAGIC_BIT_LEN 1
#endif

//enum {USX_ALPHA = 0, USX_SYM, USX_NUM, USX_DICT, USX_DELTA};

/// Default Horizontal codes. When composition of text is know beforehand, the other hcodes in this section can be used to achieve more compression.
#define USX_HCODES_DFLT (const unsigned char[]) {0x00, 0x40, 0x80, 0xC0, 0xE0}
/// Length of each default hcode
#define USX_HCODE_LENS_DFLT (const unsigned char[]) {2, 2, 2, 3, 3}

/// Horizontal codes preset for English Alphabet content only
#define USX_HCODES_ALPHA_ONLY (const unsigned char[]) {0x00, 0x00, 0x00, 0x00, 0x00}
/// Length of each Alpha only hcode
#define USX_HCODE_LENS_ALPHA_ONLY (const unsigned char[]) {0, 0, 0, 0, 0}

/// Horizontal codes preset for Alpha Numeric content only
#define USX_HCODES_ALPHA_NUM_ONLY (const unsigned char[]) {0x00, 0x00, 0x80, 0x00, 0x00}
/// Length of each Alpha numeric hcode
#define USX_HCODE_LENS_ALPHA_NUM_ONLY (const unsigned char[]) {1, 0, 1, 0, 0}

/// Horizontal codes preset for Alpha Numeric and Symbol content only
#define USX_HCODES_ALPHA_NUM_SYM_ONLY (const unsigned char[]) {0x00, 0x80, 0xC0, 0x00, 0x00}
/// Length of each Alpha numeric and symbol hcodes
#define USX_HCODE_LENS_ALPHA_NUM_SYM_ONLY (const unsigned char[]) {1, 2, 2, 0, 0}

/// Horizontal codes preset favouring Alphabet content
#define USX_HCODES_FAVOR_ALPHA (const unsigned char[]) {0x00, 0x80, 0xA0, 0xC0, 0xE0}
/// Length of each hcode favouring Alpha content
#define USX_HCODE_LENS_FAVOR_ALPHA (const unsigned char[]) {1, 3, 3, 3, 3}

/// Horizontal codes preset favouring repeating sequences
#define USX_HCODES_FAVOR_DICT (const unsigned char[]) {0x00, 0x40, 0xC0, 0x80, 0xE0}
/// Length of each hcode favouring repeating sequences
#define USX_HCODE_LENS_FAVOR_DICT (const unsigned char[]) {2, 2, 3, 2, 3}

/// Horizontal codes preset favouring symbols
#define USX_HCODES_FAVOR_SYM (const unsigned char[]) {0x80, 0x00, 0xA0, 0xC0, 0xE0}
/// Length of each hcode favouring symbols
#define USX_HCODE_LENS_FAVOR_SYM (const unsigned char[]) {3, 1, 3, 3, 3}

//#define USX_HCODES_FAVOR_UMLAUT {0x00, 0x40, 0xE0, 0xC0, 0x80}
//#define USX_HCODE_LENS_FAVOR_UMLAUT {2, 2, 3, 3, 2}

/// Horizontal codes preset favouring umlaut letters
#define USX_HCODES_FAVOR_UMLAUT (const unsigned char[]) {0x80, 0xA0, 0xC0, 0xE0, 0x00}
/// Length of each hcode favouring umlaut letters
#define USX_HCODE_LENS_FAVOR_UMLAUT (const unsigned char[]) {3, 3, 3, 3, 1}

/// Horizontal codes preset for no repeating sequences
#define USX_HCODES_NO_DICT (const unsigned char[]) {0x00, 0x40, 0x80, 0x00, 0xC0}
/// Length of each hcode for no repeating sequences
#define USX_HCODE_LENS_NO_DICT (const unsigned char[]) {2, 2, 2, 0, 2}

/// Horizontal codes preset for no Unicode characters
#define USX_HCODES_NO_UNI (const unsigned char[]) {0x00, 0x40, 0x80, 0xC0, 0x00}
/// Length of each hcode for no Unicode characters
#define USX_HCODE_LENS_NO_UNI (const unsigned char[]) {2, 2, 2, 2, 0}

/// Default frequently occuring sequences. When composition of text is know beforehand, the other sequences in this section can be used to achieve more compression.
#define USX_FREQ_SEQ_DFLT (const char *[]) {"\": \"", "\": ", "</", "=\"", "\":\"", "://"}
/// Frequently occuring sequences in text content
#define USX_FREQ_SEQ_TXT (const char *[]) {" the ", " and ", "tion", " with", "ing", "ment"}
/// Frequently occuring sequences in URL content
#define USX_FREQ_SEQ_URL (const char *[]) {"https://", "www.", ".com", "http://", ".org", ".net"}
/// Frequently occuring sequences in JSON content
#define USX_FREQ_SEQ_JSON (const char *[]) {"\": \"", "\": ", "\",", "}}}", "\":\"", "}}"}
/// Frequently occuring sequences in HTML content
#define USX_FREQ_SEQ_HTML (const char *[]) {"</", "=\"", "div", "href", "class", "<p>"}
/// Frequently occuring sequences in XML content
#define USX_FREQ_SEQ_XML (const char *[]) {"</", "=\"", "\">", "<?xml version=\"1.0\"", "xmlns:", "://"}

/// Commonly occuring templates (ISO Date/Time, ISO Date, US Phone number, ISO Time, Unused)
#define USX_TEMPLATES (const char *[]) {"tfff-of-tfTtf:rf:rf.fffZ", "tfff-of-tf", "(fff) fff-ffff", "tf:rf:rf", 0}

/// Default preset parameter set. When composition of text is know beforehand, the other parameter sets in this section can be used to achieve more compression.
#define USX_PSET_DFLT USX_HCODES_DFLT, USX_HCODE_LENS_DFLT, USX_FREQ_SEQ_DFLT, USX_TEMPLATES
/// Preset parameter set for English Alphabet only content
#define USX_PSET_ALPHA_ONLY USX_HCODES_ALPHA_ONLY, USX_HCODE_LENS_ALPHA_ONLY, USX_FREQ_SEQ_TXT, USX_TEMPLATES
/// Preset parameter set for Alpha numeric content
#define USX_PSET_ALPHA_NUM_ONLY USX_HCODES_ALPHA_NUM_ONLY, USX_HCODE_LENS_ALPHA_NUM_ONLY, USX_FREQ_SEQ_TXT, USX_TEMPLATES
/// Preset parameter set for Alpha numeric and symbol content
#define USX_PSET_ALPHA_NUM_SYM_ONLY USX_HCODES_ALPHA_NUM_SYM_ONLY, USX_HCODE_LENS_ALPHA_NUM_SYM_ONLY, USX_FREQ_SEQ_DFLT, USX_TEMPLATES
/// Preset parameter set for Alpha numeric symbol content having predominantly text
#define USX_PSET_ALPHA_NUM_SYM_ONLY_TXT USX_HCODES_ALPHA_NUM_SYM_ONLY, USX_HCODE_LENS_ALPHA_NUM_SYM_ONLY, USX_FREQ_SEQ_DFLT, USX_TEMPLATES
/// Preset parameter set favouring Alphabet content
#define USX_PSET_FAVOR_ALPHA USX_HCODES_FAVOR_ALPHA, USX_HCODE_LENS_FAVOR_ALPHA, USX_FREQ_SEQ_TXT, USX_TEMPLATES
/// Preset parameter set favouring repeating sequences
#define USX_PSET_FAVOR_DICT USX_HCODES_FAVOR_DICT, USX_HCODE_LENS_FAVOR_DICT, USX_FREQ_SEQ_DFLT, USX_TEMPLATES
/// Preset parameter set favouring symbols
#define USX_PSET_FAVOR_SYM USX_HCODES_FAVOR_SYM, USX_HCODE_LENS_FAVOR_SYM, USX_FREQ_SEQ_DFLT, USX_TEMPLATES
/// Preset parameter set favouring unlaut letters
#define USX_PSET_FAVOR_UMLAUT USX_HCODES_FAVOR_UMLAUT, USX_HCODE_LENS_FAVOR_UMLAUT, USX_FREQ_SEQ_DFLT, USX_TEMPLATES
/// Preset parameter set for when there are no repeating sequences
#define USX_PSET_NO_DICT USX_HCODES_NO_DICT, USX_HCODE_LENS_NO_DICT, USX_FREQ_SEQ_DFLT, USX_TEMPLATES
/// Preset parameter set for when there are no unicode symbols
#define USX_PSET_NO_UNI USX_HCODES_NO_UNI, USX_HCODE_LENS_NO_UNI, USX_FREQ_SEQ_DFLT, USX_TEMPLATES
/// Preset parameter set for when there are no unicode symbols favouring text
#define USX_PSET_NO_UNI_FAVOR_TEXT USX_HCODES_NO_UNI, USX_HCODE_LENS_NO_UNI, USX_FREQ_SEQ_TXT, USX_TEMPLATES
/// Preset parameter set favouring URL content
#define USX_PSET_URL USX_HCODES_DFLT, USX_HCODE_LENS_DFLT, USX_FREQ_SEQ_URL, USX_TEMPLATES
/// Preset parameter set favouring JSON content
#define USX_PSET_JSON USX_HCODES_DFLT, USX_HCODE_LENS_DFLT, USX_FREQ_SEQ_JSON, USX_TEMPLATES
/// Preset parameter set favouring JSON content having no Unicode symbols
#define USX_PSET_JSON_NO_UNI USX_HCODES_NO_UNI, USX_HCODE_LENS_NO_UNI, USX_FREQ_SEQ_JSON, USX_TEMPLATES
/// Preset parameter set favouring XML content
#define USX_PSET_XML USX_HCODES_DFLT, USX_HCODE_LENS_DFLT, USX_FREQ_SEQ_XML, USX_TEMPLATES
/// Preset parameter set favouring HTML content
#define USX_PSET_HTML USX_HCODES_DFLT, USX_HCODE_LENS_DFLT, USX_FREQ_SEQ_HTML, USX_TEMPLATES

/**
 * This structure is used when a string array needs to be compressed.
 * This is passed as a parameter to the unishox2_decompress_lines() function
 */
struct us_lnk_lst {
  char *data;
  struct us_lnk_lst *previous;
};

/**
 * This macro is for internal use, but builds upon the macro UNISHOX_API_WITH_OUTPUT_LEN
 * When the macro UNISHOX_API_WITH_OUTPUT_LEN is defined, the all the API functions
 * except the simple API functions accept an additional parameter olen
 * that enables the developer to pass the size of the output buffer provided
 * so that the api function may not write beyond that length.
 * This can be disabled if the developer knows that the buffer provided is sufficient enough
 * so no additional parameter is passed and the program is faster since additional check
 * for output length is not performed at each step
 */
#if defined(UNISHOX_API_WITH_OUTPUT_LEN) && UNISHOX_API_WITH_OUTPUT_LEN != 0
#  define UNISHOX_API_OUT_AND_LEN(out, olen) out, olen
#else
#  define UNISHOX_API_OUT_AND_LEN(out, olen) out
#endif

/** 
 * Simple API for compressing a string
 * @param[in] in    Input ASCII / UTF-8 string
 * @param[in] len   length in bytes
 * @param[out] out  output buffer - should be large enough to hold compressed output
 */
extern int unishox2_compress_simple(const char *in, int len, char *out);
/** 
 * Simple API for decompressing a string
 * @param[in] in    Input compressed bytes (output of unishox2_compress functions)
 * @param[in] len   length of 'in' in bytes
 * @param[out] out  output buffer for ASCII / UTF-8 string - should be large enough
 */
extern int unishox2_decompress_simple(const char *in, int len, char *out);
/** 
 * Comprehensive API for compressing a string
 * 
 * Presets are available for the last four parameters so they can be passed as single parameter. \n
 * See USX_PSET_* macros. Example call: \n
 *    unishox2_compress(in, len, out, olen, USX_PSET_ALPHA_ONLY);
 * 
 * @param[in] in             Input ASCII / UTF-8 string
 * @param[in] len            length in bytes
 * @param[out] out           output buffer - should be large enough to hold compressed output
 * @param[in] olen           length of 'out' buffer in bytes. Can be omitted if sufficient buffer is provided
 * @param[in] usx_hcodes     Horizontal codes (array of bytes). See macro section for samples.
 * @param[in] usx_hcode_lens Length of each element in usx_hcodes array
 * @param[in] usx_freq_seq   Frequently occuring sequences. See USX_FREQ_SEQ_* macros for samples
 * @param[in] usx_templates  Templates of frequently occuring patterns. See USX_TEMPLATES macro.
 */
extern int unishox2_compress(const char *in, int len, UNISHOX_API_OUT_AND_LEN(char *out, int olen),
              const unsigned char usx_hcodes[], const unsigned char usx_hcode_lens[],
              const char *usx_freq_seq[], const char *usx_templates[]);
/** 
 * Comprehensive API for de-compressing a string
 * 
 * Presets are available for the last four parameters so they can be passed as single parameter. \n
 * See USX_PSET_* macros. Example call: \n
 *    unishox2_decompress(in, len, out, olen, USX_PSET_ALPHA_ONLY);
 * 
 * @param[in] in             Input compressed bytes (output of unishox2_compress functions)
 * @param[in] len            length of 'in' in bytes
 * @param[out] out           output buffer - should be large enough to hold de-compressed output
 * @param[in] olen           length of 'out' buffer in bytes. Can be omitted if sufficient buffer is provided
 * @param[in] usx_hcodes     Horizontal codes (array of bytes). See macro section for samples.
 * @param[in] usx_hcode_lens Length of each element in usx_hcodes array
 * @param[in] usx_freq_seq   Frequently occuring sequences. See USX_FREQ_SEQ_* macros for samples
 * @param[in] usx_templates  Templates of frequently occuring patterns. See USX_TEMPLATES macro.
 */
extern int unishox2_decompress(const char *in, int len, UNISHOX_API_OUT_AND_LEN(char *out, int olen),
              const unsigned char usx_hcodes[], const unsigned char usx_hcode_lens[],
              const char *usx_freq_seq[], const char *usx_templates[]);
/** 
 * More Comprehensive API for compressing array of strings
 * 
 * See unishox2_compress() function for parameter definitions. \n
 * This function takes an additional parameter, i.e. 'prev_lines' - the usx_lnk_lst structure \n
 * See -g parameter in test_unishox2.c to find out how this can be used. \n
 * This function is used when an array of strings need to be compressed \n
 * and stored in a compressed array of bytes for use as a constant in other programs \n
 * where each element of the array can be decompressed and used at runtime.
 */
extern int unishox2_compress_lines(const char *in, int len, UNISHOX_API_OUT_AND_LEN(char *out, int olen),
              const unsigned char usx_hcodes[], const unsigned char usx_hcode_lens[],
              const char *usx_freq_seq[], const char *usx_templates[],
              struct us_lnk_lst *prev_lines);
/** 
 * More Comprehensive API for de-compressing array of strings \n
 * This function is not be used in conjuction with unishox2_compress_lines()
 * 
 * See unishox2_decompress() function for parameter definitions. \n
 * Typically an array is compressed using unishox2_compress_lines() and \n
 * a header (.h) file is generated using the resultant compressed array. \n
 * This header file can be used in another program with another decompress \n
 * routine which takes this compressed array as parameter and index to be \n
 * decompressed.
 */
extern int unishox2_decompress_lines(const char *in, int len, UNISHOX_API_OUT_AND_LEN(char *out, int olen),
              const unsigned char usx_hcodes[], const unsigned char usx_hcode_lens[],
              const char *usx_freq_seq[], const char *usx_templates[],
              struct us_lnk_lst *prev_lines);

#endif
