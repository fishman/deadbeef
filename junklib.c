/*
  This file is part of Deadbeef Player source code
  http://deadbeef.sourceforge.net

  library for reading tags from various audio files

  Copyright (C) 2009-2013 Alexey Yakovenko

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Alexey Yakovenko waker@users.sourceforge.net
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "junklib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_ICONV
  #define LIBICONV_PLUG
  #include <iconv.h>
#elif HAVE_ICU
  #warning icu
  #include <unicode/utypes.h>
  #include <unicode/ucnv.h>
#else
  #define DDB_RECODE
  #include "ConvertUTF/ConvertUTF.h"
uint16_t sj_to_unicode[] = {
  #include "sj_to_unicode.h"
};
#endif
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <assert.h>
#include "playlist.h"
#include "utf8.h"
#include "plugins.h"
#include "conf.h"

int enable_cp1251_detection = 1;
int enable_cp936_detection = 0;

#define MAX_TEXT_FRAME_SIZE 1024
#define MAX_CUESHEET_FRAME_SIZE 10000
#define MAX_APEV2_FRAME_SIZE 2000000
#define MAX_ID3V2_FRAME_SIZE 100000
#define MAX_ID3V2_APIC_FRAME_SIZE 2000000

#define UTF8_STR "utf-8"

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))


// mapping between ddb metadata names and id3v2/apev2 names
#define FRAME_MAPPINGS 5
enum {
    MAP_DDB = 0,
    MAP_ID3V23 = 1,
    MAP_ID3V24 = 2,
    MAP_ID3V22 = 3,
    MAP_APEV2 = 4
};

// map of known id3v2 and apev2 text tags
// order: ddb, id3-2.3, 2.4, 2.2, apev2
static const char *frame_mapping[] = {
// these tags will be displayed and edited uniformly for all tag types
    "artist", "TPE1", "TPE1", "TP1", "Artist",
    "disc", "TPOS", "TPOS", "TPA", "Disc",
    "title", "TIT2", "TIT2", "TT2", "Title",
    "album", "TALB", "TALB", "TAL", "Album",
    "copyright", "TCOP", "TCOP", "TCO", "Copyright",
    "genre", "TCON", "TCON", "TCO", "Genre",
    "composer", "TCOM", "TCOM", "TCM", "Composer",
    "year", "TYER", "TDRC", "TYE", "Year", // NOTE: TDRC and TYER are slightly different, and are converted on read/write
    "track", "TRCK", "TRCK", "TRK", "Track", // NOTE: this is special case when writing id3v2
// misc id3v2 fields
// these might or might not have appropriate fields in every tag type
    "BAND", "TPE2", "TPE2", "TP2", NULL,
    "ENCODER", "TENC", "TENC", "TEN", NULL,
    "BEATS_PER_MINUTE", "TBPM", "TBPM", "TBP", NULL,
    "PLAYLIST_DELAY", "TDLY", "TDLY", "TDY", NULL,
    "TEXT_WRITERS", "TEXT", "TEXT", "TXT", NULL,
    "FILE_TYPE", "TFLT", "TFLT", "TFT", NULL,
    "CONTENT_GROUP_DESCRIPTION", "TIT1", "TIT1", "TT1", NULL,
    "SUBTITLE", "TIT3", "TIT3", "TT3", NULL,
    "INITIAL_KEY", "TKEY", "TKEY", "TKE", NULL,
    "LANGUAGES", "TLAN", "TLAN", "TLA", NULL,
    "LENGTH", "TLEN", "TLEN", "TLE", NULL,
    "MEDIA_TYPE", "TMED", "TMED", "TMT", NULL,
    "ORIGINAL_ALBUM_TITLE", "TOAL", "TOAL", "TOT", NULL,
    "ORIGINAL_FILENAME", "TOFN", "TOFN", "TOF", NULL,
    "ORIGINAL_TEXT_WRITERS", "TOLY", "TOLY", "TOL", NULL,
    "ORIGINAL_ARTISTS", "TOPE", "TOPE", "TOA", NULL,
    "FILE_OWNER", "TOWN", "TOWN", NULL, NULL,
    "PERFORMER_REFINEMENT", "TPE3", "TPE3", "TP3", NULL,
    "MODIFIED_BY", "TPE4", "TPE4", "TP4", NULL,
    "PUBLISHER", "TPUB", "TPUB", "TPB", NULL,
    "INTERNET_RADIO_STATION_NAME", "TRSN", "TRSN", NULL, NULL,
    "INTERNET_RADIO_STATION_OWNER", "TRSO", "TRSO", NULL, NULL,
    "ISRC", "TSRC", "TSRC", NULL, NULL,
    "ENCODING_SOFTWARE_HARDWARE", "TSSE", "TSSE", "TSS", NULL,
    "RECORDING_TIME", NULL, "TDRC", NULL, NULL,
    "RELEASE_TIME", NULL, "TDRL", NULL, NULL,
    "TAGGING_TIME", NULL, "TDTG", NULL, NULL,
    "ALBUM_SORT_ORDER", NULL, "TSOA", NULL, NULL,
    "PERFORMER_SORT_ORDER", NULL, "TSOP", NULL, NULL,
    "TITLE_SORT_ORDER", NULL, "TSOT", NULL, NULL,
    "SIZE", "TSIZ", NULL, "TSI", NULL,
    "RECORDING_DATES", "TRDA", NULL, "TRD", NULL,
    "INVOLVED_PEOPLE_LIST", NULL, "TIPL", NULL, NULL,
    "MUSICIAN_CREDITS_LIST", NULL, "TMCL", NULL, NULL,
    "ENCODING_TIME", NULL, "TDEN", NULL, NULL,
    "ORIGINAL_RELEASE_TIME", NULL, "TDOR", NULL, NULL,
    "MOOD", NULL, "TMOO", NULL, NULL,
    "PRODUCED_NOTICE", NULL, "TPRO", NULL, NULL,
    NULL
};

static const char *txx_mapping[] = {
    "replaygain_album_gain", NULL,
    "replaygain_album_peak", NULL,
    "replaygain_track_gain", NULL,
    "replaygain_track_peak", NULL,
    NULL
};

static uint32_t
extract_i32 (const uint8_t *buf)
{
    uint32_t x;
    // big endian extract

    x = buf[0];
    x <<= 8;
    x |= buf[1];
    x <<= 8;
    x |= buf[2];
    x <<= 8;
    x |= buf[3];

    return x;
}

static inline uint32_t
extract_i32_le (unsigned char *buf)
{
    uint32_t x;
    // little endian extract

    x = buf[3];
    x <<= 8;
    x |= buf[2];
    x <<= 8;
    x |= buf[1];
    x <<= 8;
    x |= buf[0];

    return x;
}

static inline uint16_t
extract_i16 (const uint8_t *buf)
{
    uint16_t x;
    // big endian extract

    x = buf[0];
    x <<= 8;
    x |= buf[1];

    return x;
}

static inline float
extract_f32 (unsigned char *buf) {
    float f;
    uint32_t *x = (uint32_t *)&f;
    *x = buf[0];
    *x <<= 8;
    *x |= buf[1];
    *x <<= 8;
    *x |= buf[2];
    *x <<= 8;
    *x |= buf[3];
    return f;
}

#ifdef DDB_RECODE

static int
cp1251_to_utf8(const uint8_t *in, int inlen, uint8_t *out, int outlen){
    int len = 0;
    while (inlen > 0 && outlen-len > 2) {
        uint8_t c=*in;
        if (c>=192 && c<=239) {
            *out++ = 208;
            *out++ = c - 48;
            len += 2;
        }
        else if (c>239) {
            *out++ = 209;
            *out++ = c - 112;
            len += 2;
        }
        else if (c==184) {
            *out++ = 0xd1;
            *out++ = 0x91;
            len += 2;
        }
        else if (c==168) {
            *out++ = 208;
            *out++ = 129;
            len += 2;
        }
        else {
            *out++ = c;
            len++;
        }
        in++;
        inlen--;
    }
    *out = 0;
    return len;
}

static int
iso8859_1_to_utf8(const uint8_t *in, int inlen, uint8_t *out, int outlen){
    int len = 0;
    while (inlen > 0 && outlen-len > 2) {
        uint8_t c=*in;

        switch (c) {
        case 192 ... 255:
            *out++ = 195;
            *out++ = c - 64;
            len += 2;
            break;
        case 160 ... 191:
            *out++ = 0xc2;
            *out++ = c;
            len += 2;
            break;
#define CONV2(x,y,z) case x:\
            *out++ = y;\
            *out++ = z;\
            len += 2;\
            break;
#define CONV3(x,y,z,w) case x:\
            *out++ = y;\
            *out++ = z;\
            *out++ = w;\
            len += 3;\
            break;
        CONV2(0x9f,0xc5,0xb8);
        CONV2(0x9e,0xc5,0xbe);
        CONV3(0x9d,0xef,0xbf,0xbd);
        CONV3(0x80,0xe2,0x82,0xac);
        CONV3(0x81,0xef,0xbf,0xbd);
        CONV3(0x82,0xe2,0x80,0x9a);
        CONV2(0x83,0xc6,0x92);
        CONV3(0x84,0xe2,0x80,0x9e);
        CONV3(0x85,0xe2,0x80,0xa6);
        CONV3(0x86,0xe2,0x80,0xa0);
        CONV3(0x87,0xe2,0x80,0xa1);
        CONV2(0x88,0xcb,0x86);
        CONV3(0x89,0xe2,0x80,0xb0);
        CONV2(0x8a,0xc5,0xa0);
        CONV3(0x8b,0xe2,0x80,0xb9);
        CONV2(0x8c,0xc5,0x92);
        CONV3(0x8d,0xef,0xbf,0xbd);
        CONV2(0x8e,0xc5,0xbd);
        CONV3(0x8f,0xef,0xbf,0xbd);
        CONV3(0x90,0xef,0xbf,0xbd);
        CONV3(0x91,0xe2,0x80,0x98);
        CONV3(0x92,0xe2,0x80,0x99);
        CONV3(0x93,0xe2,0x80,0x9c);
        CONV3(0x94,0xe2,0x80,0x9d);
        CONV3(0x95,0xe2,0x80,0xa2);
        CONV3(0x96,0xe2,0x80,0x93);
        CONV3(0x97,0xe2,0x80,0x94);
        CONV2(0x98,0xcb,0x9c);
        CONV3(0x99,0xe2,0x84,0xa2);
        CONV2(0x9a,0xc5,0xa1);
        CONV3(0x9b,0xe2,0x80,0xba);
        CONV2(0x9c,0xc5,0x93);
#undef CONV2
#undef CONV3
        default:
            if (c >= 127) {
                trace ("iso8859 char: %d\n", c);
            }
            *out++ = c;
            len++;
            break;
        }

        in++;
        inlen--;
    }
    *out = 0;
    return len;
}

int ddb_iconv (const char *cs_out, const char *cs_in, char *out, int outlen, const char *in, int inlen) {
    int len = -1;
    *out = 0;
    if (inlen==0) {
        return 0;
    }
    // to utf8 branch
    if (!strcasecmp (cs_out, UTF8_STR)) {
        if (!strcasecmp (cs_in, UTF8_STR)) {
            memcpy (out, in, inlen);
            out[inlen] = 0;
            int valid = u8_valid (out, inlen, NULL);
            if (valid) {
                len = inlen;
            }
        }
        else if (!strcasecmp (cs_in, "cp1251")) {
            len = cp1251_to_utf8 (in, inlen, out, outlen);
        }
        else if (!strcasecmp (cs_in, "iso8859-1")) {
            len = iso8859_1_to_utf8 (in, inlen, out, outlen);
        }
        else if (!strcasecmp (cs_in, "UTF-16LE") || !strcasecmp (cs_in, "UCS-2LE")) {
            char *target = out;
            ConversionResult result = ConvertUTF16toUTF8 ((const UTF16**)&in, (const UTF16*)(in + inlen), (UTF8**)&target, (UTF8*)(out + outlen), strictConversion);
            if (result == conversionOK) {
                *target = 0;
                len = target - out;
            }
        }
        else if (!strcasecmp (cs_in, "UTF-16BE") || !strcasecmp (cs_in, "UCS-2BE")) {
            assert (0);
            // convert to big endian
            char temp[inlen];
            for (int i = 0; i < inlen; i += 2) {
                temp[i] = in[i+1];
                temp[i+1] = in[i];
            }
            char *target = out;
            char *src = temp;
            ConversionResult result = ConvertUTF16toUTF8 ((const UTF16**)&src, (const UTF16*)(temp + inlen), (UTF8**)&target, (UTF8*)(out + outlen), strictConversion);
            if (result == conversionOK) {
                *target = 0;
                len = target - out;
            }
        }
        else if (!strcasecmp (cs_in, "SHIFT-JIS")) {
            int len = 0;
            while (inlen > 0 && len < outlen) {
                if (*in > 0) {
                    *out++ = *in++;
                    inlen--;
                    len++;
                }
                else if (inlen < 2) {
                    return -1;
                }
                else {
                    // find character in table
                    uint16_t c = (((uint8_t*)in)[0] << 8) | ((uint8_t*)in)[1];
                    int i;
                    for (i = 0; sj_to_unicode[i]; i += 2) {
                        if (c == sj_to_unicode[i]) {
                            break;
                        }
                    }
                    if (sj_to_unicode[i]) {
                        // slow conversion!
                        char unicode_val[2] = { (sj_to_unicode[i+1] & 0xff00) >> 8, sj_to_unicode[i+1] & 0xff };
                        char utf8_val[5];
                        char *src = unicode_val, *dst = utf8_val;

                        ConversionResult res = ConvertUTF16toUTF8 ((const UTF16**)&src, (const UTF16 *)(src+2), (UTF8**)&dst, dst+5, strictConversion);
                        if (res == conversionOK) {
                            if (src - utf8_val < outlen-len) {
                                memcpy (out, utf8_val, src - utf8_val);
                                out += src - utf8_val;
                                len += src - utf8_val;
                                inlen -= 2;
                                in += 2;
                            }
                            else {
                                return -1;
                            }
                        }
                        else {
                            return -1;
                        }
                    }
                    else {
                        return -1; // error
                    }
                }
            }
        }
        else {
            fprintf (stderr, "invalid conversion request: %s -> %s\n", cs_in, cs_out);
        }
    }
    else if (!strcasecmp (cs_in, UTF8_STR)) {
        if (!strcasecmp (cs_out, "UTF-16LE") || !strcasecmp (cs_out, "UCS-2LE")) {
            char *target = out;
            ConversionResult result = ConvertUTF8toUTF16 ((const UTF8**)&in, (const UTF8*)(in + inlen), (UTF16**)&target, (UTF16*)(out + outlen), strictConversion);
            if (result == conversionOK) {
                *target = 0;
                *(target+1) = 0;
                len = target - out;
            }
        }
        else if (!strcasecmp (cs_out, "UTF-16BE") || !strcasecmp (cs_out, "UCS-2BE")) {
            assert (0);
            char temp[outlen];
            char *target = temp;
            const char *src = in;
            ConversionResult result = ConvertUTF16toUTF8 ((const UTF16**)&src, (const UTF16*)(src + inlen), (UTF8**)&target, (UTF8*)(target + outlen), strictConversion);
            if (result == conversionOK) {
                len = target - temp;
                // convert to big endian
                for (int i = 0; i < len; i += 2) {
                    out[i] = temp[i+1];
                    out[i+1] = temp[i];
                }
            }
        }
        else {
            fprintf (stderr, "invalid conversion request: %s -> %s\n", cs_in, cs_out);
        }
    }
    else {
        fprintf (stderr, "invalid conversion request: %s -> %s\n", cs_in, cs_out);
    }
    trace ("\033[0;31mddb_iconv: %s -> %s, in: %s, out: %s\033[37;0m\n", cs_in, cs_out, in, out);
    return len;
}
#endif

int
junk_iconv (const char *in, int inlen, char *out, int outlen, const char *cs_in, const char *cs_out) {
// NOTE: this function must support utf8->utf8 conversion, used for validation
#if HAVE_ICONV
    iconv_t cd = iconv_open (cs_out, cs_in);
    if (cd == (iconv_t)-1) {
        return -1;
    }
#ifdef __linux__
    char *pin = (char*)in;
#else
    const char *pin = in;
#endif

    size_t inbytesleft = inlen;
    size_t outbytesleft = outlen;

    char *pout = out;

    size_t res = iconv (cd, &pin, &inbytesleft, &pout, &outbytesleft);
    int err = errno;
    iconv_close (cd);

    //trace ("iconv -f %s -t %s '%s': returned %d, inbytes %d/%d, outbytes %d/%d, errno=%d\n", cs_in, cs_out, in, (int)res, inlen, (int)inbytesleft, outlen, (int)outbytesleft, err);
    if (res == -1) {
        return -1;
    }
    out[pout-out] = 0;
    //trace ("iconv out: %s (len=%d)\n", out, pout - out);
    return pout - out;
#elif defined(HAVE_ICU)
    int status = 0;
    trace ("ICU convert from %s to %s input %s\n", cs_in, cs_out, in);
    int32_t len = ucnv_convert (cs_out, cs_in, out, outlen, in, inlen, &status);
    out[len] = 0;
    trace ("ICU out: %s\n", out);
    return len;
#else
    int len = ddb_iconv (cs_out, cs_in, out, outlen, in, inlen);
    return len;
#endif
}


static const char *junk_genretbl[] = {
    "Blues",
    "Classic Rock",
    "Country",
    "Dance",
    "Disco",
    "Funk",
    "Grunge",
    "Hip-Hop",
    "Jazz",
    "Metal",
    "New Age",
    "Oldies",
    "Other",
    "Pop",
    "R&B",
    "Rap",
    "Reggae",
    "Rock",
    "Techno",
    "Industrial",
    "Alternative",
    "Ska",
    "Death Metal",
    "Pranks",
    "Soundtrack",
    "Euro-Techno",
    "Ambient",
    "Trip-Hop",
    "Vocal",
    "Jazz+Funk",
    "Fusion",
    "Trance",
    "Classical",
    "Instrumental",
    "Acid",
    "House",
    "Game",
    "Sound Clip",
    "Gospel",
    "Noise",
    "AlternRock",
    "Bass",
    "Soul",
    "Punk",
    "Space",
    "Meditative",
    "Instrumental Pop",
    "Instrumental Rock",
    "Ethnic",
    "Gothic",
    "Darkwave",
    "Techno-Industrial",
    "Electronic",
    "Pop-Folk",
    "Eurodance",
    "Dream",
    "Southern Rock",
    "Comedy",
    "Cult",
    "Gangsta",
    "Top 40",
    "Christian Rap",
    "Pop/Funk",
    "Jungle",
    "Native American",
    "Cabaret",
    "New Wave",
    "Psychadelic",
    "Rave",
    "Showtunes",
    "Trailer",
    "Lo-Fi",
    "Tribal",
    "Acid Punk",
    "Acid Jazz",
    "Polka",
    "Retro",
    "Musical",
    "Rock & Roll",
    "Hard Rock",
    "Folk",
    "Folk-Rock",
    "National Folk",
    "Swing",
    "Fast Fusion",
    "Bebob",
    "Latin",
    "Revival",
    "Celtic",
    "Bluegrass",
    "Avantgarde",
    "Gothic Rock",
    "Progressive Rock",
    "Psychedelic Rock",
    "Symphonic Rock",
    "Slow Rock",
    "Big Band",
    "Chorus",
    "Easy Listening",
    "Acoustic",
    "Humour",
    "Speech",
    "Chanson",
    "Opera",
    "Chamber Music",
    "Sonata",
    "Symphony",
    "Booty Bass",
    "Primus",
    "Porn Groove",
    "Satire",
    "Slow Jam",
    "Club",
    "Tango",
    "Samba",
    "Folklore",
    "Ballad",
    "Power Ballad",
    "Rhythmic Soul",
    "Freestyle",
    "Duet",
    "Punk Rock",
    "Drum Solo",
    "Acapella",
    "Euro-House",
    "Dance Hall",
    "Goa",
    "Drum & Bass",
    "Club-House",
    "Hardcore",
    "Terror",
    "Indie",
    "BritPop",
    "Negerpunk",
    "Polsk Punk",
    "Beat",
    "Christian Gangsta",
    "Heavy Metal",
    "Black Metal",
    "Crossover",
    "Contemporary C",
    "Christian Rock",
    "Merengue",
    "Salsa",
    "Thrash Metal",
    "Anime",
    "JPop",
    "SynthPop",
    NULL
};

static int
can_be_russian (const signed char *str, int size) {
    if (!enable_cp1251_detection) {
        return 0;
    }
    int latin = 0;
    int rus = 0;
    int rus_in_row = 0;
    int max_rus_row = 0;
    int n = 0;
    for (; n < size; str++, n++) {
        if ((*str >= 'A' && *str <= 'Z')
                || *str >= 'a' && *str <= 'z') {
            if (rus_in_row > max_rus_row) {
                max_rus_row = rus_in_row;
            }
            rus_in_row = 0;
            latin++;
        }
        else if (*str < 0) {
            rus_in_row++;
            rus++;
        }
    }
    if (rus > latin/2 || (max_rus_row > 4)) {
        return 1;
    }
    return 0;
}

static int
can_be_chinese (const uint8_t *str, int sz) {
    if (!enable_cp936_detection) {
        return 0;
    }
    int len = strlen (str);
    for (int i = 0; i < sz; str++, i++) {
        if (i < len-3
                && (*str >= 0x81 && *str <= 0xFE )
                && (*(str+1) >= 0x30 && *(str+1) <= 0x39)
                && (*(str+2) >= 0x81 && *(str+2) <= 0xFE)
                && (*(str+3) >= 0x30 && *(str+3) <= 0x39)) {
            return 1;
        }
        if (i < len - 1
                && (*str >= 0x81 && *str <= 0xFE )
                && ((*(str+1) >= 0x40 && *(str+1) <= 0x7E)
                    || (*(str+1) >= 0x80 && *(str+1) <= 0xFE))) {
            return 1;
        }
    }
    return 0;
}

static char *
convstr_id3v2 (int version, uint8_t encoding, const unsigned char* str, int sz) {
    char out[2048] = "";
    const char *enc = NULL;

    // detect encoding
    if (version == 4 && encoding == 2) {
        enc = "UTF-16BE";
    }
    else if (version == 4 && encoding == 3) {
        enc = UTF8_STR;
    }
    else if (encoding == 0) {
        if (can_be_chinese (str, sz)) {
            // hack to add cp936 support
            enc = "cp936";
        }
        else if (can_be_russian (str, sz)) {
            // hack to add limited cp1251 recoding support
            enc = "cp1251";
        }
        else {
            enc = "iso8859-1";
        }
    }
    else if (encoding != 1 && !(version == 4 && encoding == 3)){
        return NULL; // invalid encoding
    }

    if (encoding == 1) { // detect kind of unicode used
        if (sz < 2) {
            return NULL;
        }
        if (version < 4) {
            if (str[0] == 0xff && str[1] == 0xfe) {
                enc = "UCS-2LE";
                str += 2;
                sz -= 2;
            }
            else if (str[1] == 0xff && str[0] == 0xfe) {
                enc = "UCS-2BE";
                str += 2;
                sz -= 2;
            }
            else {
                trace ("invalid ucs-2 signature %x %x\n", (int)str[0], (int)str[1]);
                enc = "UCS-2LE";
                // NOTE: this is an assumption, might break in the future.
            }
        }
        else {
            enc = "UTF-16";
        }
    }

    trace ("encoding: %s\n", enc);

    int converted_sz = 0;

    if ((converted_sz = junk_iconv (str, sz, out, sizeof (out), enc, UTF8_STR)) < 0) {
        return NULL;
    }
//    trace ("%s -> %s\n", str, out);
    int n;
    for (n = 0; n < converted_sz; n++) {
        if (out[n] == 0 && n != converted_sz-1) {
            out[n] = '\n';
        }
    }
    // trim trailing linebreaks
    for (n = converted_sz-1; n >= 0; n--) {
        if ((uint8_t)out[n] <= 32) {
            out[n] = 0;
        }
        else {
            break;
        }
    }
    return strdup (out);
}

static const char *
convstr_id3v1 (const char* str, int sz) {
    static char out[2048];
    int i;
    for (i = 0; i < sz; i++) {
        if (str[i] != ' ') {
            break;
        }
    }
    if (i == sz) {
        out[0] = 0;
        return out;
    }

    // check for valid utf8
    int valid = u8_valid (str, sz, NULL);
    if (valid) {
        return str;
    }
    const char *enc = "iso8859-1";
    if (can_be_chinese (str, sz)) {
        // hack to add cp936 support
        enc = "cp936";
    }
    else if (can_be_russian (str, sz)) {
        // hack to add limited cp1251 recoding support
        enc = "cp1251";
    }

    int len = junk_iconv (str, sz, out, sizeof (out), enc, UTF8_STR);
    if (len >= 0) {
        return out;
    }
    return NULL;
}

static void
str_trim_right (uint8_t *str, int len) {
    uint8_t *p = str + len - 1;
    while (p >= str && *p <= 0x20) {
        p--;
    }
    p++;
    *p = 0;
}

// should read both id3v1 and id3v1.1
int
junk_id3v1_read (playItem_t *it, DB_FILE *fp) {
    if (!it || !fp) {
        trace ("bad call to junk_id3v1_read!\n");
        return -1;
    }
    uint8_t buffer[128];
    // try reading from end
    if (deadbeef->fseek (fp, -128, SEEK_END) == -1) {
        return -1;
    }
    if (deadbeef->fread (buffer, 1, 128, fp) != 128) {
        return -1;
    }
    if (memcmp (buffer, "TAG", 3)) {
        return -1; // no tag
    }
    char title[31];
    char artist[31];
    char album[31];
    char year[5];
    char comment[31];
    uint8_t genreid;
    uint8_t tracknum;
    const char *genre = NULL;
    memset (title, 0, 31);
    memset (artist, 0, 31);
    memset (album, 0, 31);
    memset (year, 0, 5);
    memset (comment, 0, 31);
    memcpy (title, &buffer[3], 30);
    str_trim_right (title, 30);
    memcpy (artist, &buffer[3+30], 30);
    str_trim_right (artist, 30);
    memcpy (album, &buffer[3+60], 30);
    str_trim_right (album, 30);
    memcpy (year, &buffer[3+90], 4);
    str_trim_right (year, 4);
    memcpy (comment, &buffer[3+94], 30);
    str_trim_right (comment, 30);
    genreid = buffer[3+124];
    tracknum = 0;
    if (comment[28] == 0 && comment[29] != 0) {
        tracknum = comment[29];
    }
//    255 = "None",
//    "CR" = "Cover" (id3v2)
//    "RX" = "Remix" (id3v2)

    if (genreid == 0xff) {
        //genre = "None";
    }
    else if (genreid <= 147) {
        genre = junk_genretbl[genreid];
    }

    // add meta
//    trace ("%s - %s - %s - %s - %s - %s\n", title, artist, album, year, comment, genre);
    if (*title) {
        pl_add_meta (it, "title", convstr_id3v1 (title, strlen (title)));
    }
    if (*artist) {
        pl_add_meta (it, "artist", convstr_id3v1 (artist, strlen (artist)));
    }
    if (*album) {
        pl_add_meta (it, "album", convstr_id3v1 (album, strlen (album)));
    }
    if (*year) {
        pl_add_meta (it, "year", year);
    }
    if (*comment) {
        pl_add_meta (it, "comment", convstr_id3v1 (comment, strlen (comment)));
    }
    if (genre && *genre) {
        pl_add_meta (it, "genre", convstr_id3v1 (genre, strlen (genre)));
    }
    if (tracknum != 0) {
        char s[4];
        snprintf (s, 4, "%d", tracknum);
        pl_add_meta (it, "track", s);
    }

    uint32_t f = pl_get_item_flags (it);
    f |= DDB_TAG_ID3V1;
    pl_set_item_flags (it, f);

    return 0;
}

int
junk_id3v1_write (FILE *fp, playItem_t *it, const char *enc) {
    char title[30] = "";
    char artist[30] = "";
    char album[30] = "";
    char year[4] = "";
    char comment[28] = "";
    uint8_t genreid = 0xff;
    uint8_t tracknum = 0;

    const char *meta;

    pl_lock ();

#define conv(name, store) {\
    memset (store, 0x20, sizeof (store));\
    meta = pl_find_meta (it, name);\
    if (meta) {\
        char temp[1000];\
        int l = junk_iconv (meta, strlen (meta), temp, sizeof (temp), UTF8_STR, enc);\
        if (l == -1) {\
            memset (store, 0, sizeof (store));\
        }\
        else {\
            strncpy (store, temp, sizeof (store));\
        }\
        char *cr = strchr (store, '\n');\
        if (cr) {\
            *cr = 0;\
        }\
    }\
}

    conv ("title", title);
    conv ("artist", artist);
    conv ("album", album);
    conv ("year", year);
    conv ("comment", comment);

#undef conv

    // tracknum
    meta = pl_find_meta (it, "track");
    if (meta) {
        tracknum = atoi (meta);
    }

    // find genre
    meta = pl_find_meta (it, "genre");
    if (meta) {
        for (int i = 0; junk_genretbl[i]; i++) {
            if (!strcasecmp (meta, junk_genretbl[i])) {
                genreid = i;
                break;
            }
        }
    }

    pl_unlock ();

    if (fwrite ("TAG", 1, 3, fp) != 3) {
        trace ("junk_id3v1_write: failed to write signature\n");
        return -1;
    }
    if (fwrite (title, 1, sizeof (title), fp) != sizeof (title)) {
        trace ("junk_id3v1_write: failed to write title\n");
        return -1;
    }
    if (fwrite (artist, 1, sizeof (artist), fp) != sizeof (artist)) {
        trace ("junk_id3v1_write: failed to write artist\n");
        return -1;
    }
    if (fwrite (album, 1, sizeof (album), fp) != sizeof (album)) {
        trace ("junk_id3v1_write: failed to write album\n");
        return -1;
    }
    if (fwrite (year, 1, sizeof (year), fp) != sizeof (year)) {
        trace ("junk_id3v1_write: failed to write year\n");
        return -1;
    }
    if (fwrite (comment, 1, sizeof (comment), fp) != sizeof (comment)) {
        trace ("junk_id3v1_write: failed to write comment\n");
        return -1;
    }
    uint8_t zero = 0;
    if (fwrite (&zero, 1, 1, fp) != 1) {
        trace ("junk_id3v1_write: failed to write id3v1.1 marker\n");
        return -1;
    }
    if (fwrite (&tracknum, 1, 1, fp) != 1) {
        trace ("junk_id3v1_write: failed to write track\n");
        return -1;
    }
    if (fwrite (&genreid, 1, 1, fp) != 1) {
        trace ("junk_id3v1_write: failed to write genre\n");
        return -1;
    }
    return 0;
}

int
junk_id3v1_find (DB_FILE *fp) {
    uint8_t buffer[3];
    if (deadbeef->fseek (fp, -128, SEEK_END) == -1) {
        return -1;
    }
    if (deadbeef->fread (buffer, 1, 3, fp) != 3) {
        return -1;
    }
    if (memcmp (buffer, "TAG", 3)) {
        return -1; // no tag
    }
    return deadbeef->ftell (fp) - 3;
}

int
junk_apev2_find (DB_FILE *fp, int32_t *psize, uint32_t *pflags, uint32_t *pnumitems) {
    uint8_t header[32];
    if (deadbeef->fseek (fp, -32, SEEK_END) == -1) {
        return -1; // something bad happened
    }

    if (deadbeef->fread (header, 1, 32, fp) != 32) {
        return -1; // something bad happened
    }
    if (strncmp (header, "APETAGEX", 8)) {
        // try to skip 128 bytes backwards (id3v1)
        if (deadbeef->fseek (fp, -128-32, SEEK_END) == -1) {
            return -1; // something bad happened
        }
        if (deadbeef->fread (header, 1, 32, fp) != 32) {
            return -1; // something bad happened
        }
        if (strncmp (header, "APETAGEX", 8)) {
            return -1; // no ape tag here
        }
    }

    uint32_t version = extract_i32_le (&header[8]);
    int32_t size = extract_i32_le (&header[12]);
    uint32_t numitems = extract_i32_le (&header[16]);
    uint32_t flags = extract_i32_le (&header[20]);

    trace ("APEv%d, size=%d, items=%d, flags=%x\n", version, size, numitems, flags);

    // size contains footer, but not header, so add header size
    if (flags & (1<<31)) {
        size += 32;
    }

    // seek to beginning of the tag/header
    if (deadbeef->fseek (fp, -size, SEEK_CUR) == -1) {
        trace ("failed to seek to tag start (-%d)\n", size);
        return -1;
    }
    *psize = size;
    *pflags = flags;
    *pnumitems = numitems;
    return deadbeef->ftell (fp);
}

int
junk_find_id3v1 (DB_FILE *fp) {
    if (deadbeef->fseek (fp, -128, SEEK_END) == -1) {
        return -1;
    }
    char buffer[3];
    if (deadbeef->fread (buffer, 1, 3, fp) != 3) {
        return -1;
    }
    if (memcmp (buffer, "TAG", 3)) {
        return -1; // no tag
    }
    return deadbeef->ftell (fp) - 3;
}

int
junk_add_track_meta (playItem_t *it, const char *track) {
    char *slash = strchr (track, '/');
    if (slash) {
        // split into track/number
        *slash = 0;
        slash++;
        pl_add_meta (it, "numtracks", slash);
    }
    pl_add_meta (it, "track", track);
    return 0;
}

int
junk_apev2_add_frame (playItem_t *it, DB_apev2_tag_t *tag_store, DB_apev2_frame_t **tail, uint32_t itemsize, uint32_t itemflags, const char *key, const uint8_t *value) {
    if (tag_store) {
        DB_apev2_frame_t *frm = malloc (sizeof (DB_apev2_frame_t) + itemsize);
        memset (frm, 0, sizeof (DB_apev2_tag_t));
        frm->flags = itemflags;
        strcpy (frm->key, key);
        trace ("*** stored frame %s flags %X\n", key, itemflags);
        frm->size = itemsize;
        memcpy (frm->data, value, itemsize);
        if (*tail) {
            (*tail)->next = frm;
        }
        else {
            tag_store->frames = frm;
        }
        *tail = frm;
    }

    if (it) {
        int valuetype = ((itemflags >> 1) & 3);
        // add metainfo only if it's textual
        if (valuetype == 0 && (itemsize < MAX_TEXT_FRAME_SIZE || (!strcasecmp (key, "cuesheet") && itemsize < MAX_CUESHEET_FRAME_SIZE))) {
            if (!u8_valid (value, itemsize, NULL)) {
                trace ("junk_read_ape_full: bad encoding in text frame %s\n", key);
                return -1;
            }

            int m;
            for (m = 0; frame_mapping[m]; m += FRAME_MAPPINGS) {
                if (frame_mapping[m + MAP_APEV2] && !strcasecmp (key, frame_mapping[m + MAP_APEV2])) {
                    if (!strcmp (frame_mapping[m+MAP_DDB], "track")) {
                        junk_add_track_meta (it, value);
                    }
                    else {
                        trace ("pl_append_meta %s %s\n", frame_mapping[m+MAP_DDB], value);
                        pl_append_meta (it, frame_mapping[m+MAP_DDB], value);
                    }
                    break;
                }
            }

            trace ("apev2 %s=%s\n", key, value);

            if (!frame_mapping[m]) {
                if (!strncasecmp (key, "replaygain_album_gain", 21)) {
                    pl_set_item_replaygain (it, DDB_REPLAYGAIN_ALBUMGAIN, atof (value));
                    trace ("album_gain=%s\n", value);
                }
                else if (!strncasecmp (key, "replaygain_album_peak", 21)) {
                    pl_set_item_replaygain (it, DDB_REPLAYGAIN_ALBUMPEAK, atof (value));
                    trace ("album_peak=%s\n", value);
                }
                else if (!strncasecmp (key, "replaygain_track_gain", 21)) {
                    pl_set_item_replaygain (it, DDB_REPLAYGAIN_TRACKGAIN, atof (value));
                    trace ("track_gain=%s\n", value);
                }
                else if (!strncasecmp (key, "replaygain_track_peak", 21)) {
                    pl_set_item_replaygain (it, DDB_REPLAYGAIN_TRACKPEAK, atof (value));
                    trace ("track_peak=%s\n", value);
                }
                else {
                    trace ("%s=%s\n", key, value);
                    pl_append_meta (it, key, value);;
                }
            }
        }
    }
    return 0;
}

int
junk_apev2_read_full_mem (playItem_t *it, DB_apev2_tag_t *tag_store, char *mem, int memsize) {
    char *end = mem+memsize;
#define STEP(x,y) {mem+=(x);if(mem+(y)>end) {trace ("fail %d\n", (x));return -1;}}

    char *header = mem;

    DB_apev2_frame_t *tail = NULL;

    uint32_t version = extract_i32_le (&header[0]);
    int32_t size = extract_i32_le (&header[4]);
    uint32_t numitems = extract_i32_le (&header[8]);
    uint32_t flags = extract_i32_le (&header[12]);

    trace ("APEv%d, size=%d, items=%d, flags=%x\n", version, size, numitems, flags);
    if (it) {
        uint32_t f = pl_get_item_flags (it);
        f |= DDB_TAG_APEV2;
        pl_set_item_flags (it, f);
    }

    STEP(24, 8);

    int i;
    for (i = 0; i < numitems; i++) {
        trace ("reading item %d\n", i);
        uint8_t *buffer = mem;

        uint32_t itemsize = extract_i32_le (&buffer[0]);
        uint32_t itemflags = extract_i32_le (&buffer[4]);

        STEP(8, 1);
        trace ("size=%d, flags=%x\n", itemsize, itemflags);

        // read key until 0 (stupid and slow)
        char key[256];
        int keysize = 0;
        while (keysize <= 255 && mem < end) {
            key[keysize] = *mem;
            mem++;
            if (key[keysize] == 0) {
                break;
            }
            if (key[keysize] < 0x20) {
                trace ("nonascii chars\n");
                return -1; // non-ascii chars and chars with codes 0..0x1f not allowed in ape item keys
            }
            keysize++;
        }
        key[255] = 0;
        trace ("item %d, size %d, flags %08x, keysize %d, key %s\n", i, itemsize, itemflags, keysize, key);
        // read value
        if (itemsize <= MAX_APEV2_FRAME_SIZE) // just a sanity check
        {
            STEP(0,itemsize);
            uint8_t *value = malloc (itemsize+1);
            if (!value) {
                trace ("junk_read_ape_full: failed to allocate %d bytes\n", itemsize+1);
                return -1;
            }
            memcpy (value, mem, itemsize);
            value[itemsize] = 0;

            // replace 0s with \n
            uint8_t *p = value;
            while (p < value + itemsize - 1) {
                if (*p == 0) {
                    *p = '\n';
                }
                p++;
            }

            junk_apev2_add_frame (it, tag_store, &tail, itemsize, itemflags, key, value);

            free (value);
            STEP(itemsize, 8);
        }
        else {
            STEP(itemsize,8);
        }
    }
    return 0;
}

int
junk_apev2_read_full (playItem_t *it, DB_apev2_tag_t *tag_store, DB_FILE *fp) {
    // try to read footer, position must be already at the EOF right before
    // id3v1 (if present)

    DB_apev2_frame_t *tail = NULL;

    uint8_t header[32];
    if (deadbeef->fseek (fp, -32, SEEK_END) == -1) {
        return -1; // something bad happened
    }

    if (deadbeef->fread (header, 1, 32, fp) != 32) {
        return -1; // something bad happened
    }
    if (strncmp (header, "APETAGEX", 8)) {
        // try to skip 128 bytes backwards (id3v1)
        if (deadbeef->fseek (fp, -128-32, SEEK_END) == -1) {
            return -1; // something bad happened
        }
        if (deadbeef->fread (header, 1, 32, fp) != 32) {
            return -1; // something bad happened
        }
        if (strncmp (header, "APETAGEX", 8)) {
            return -1; // no ape tag here
        }
    }

    // end of footer must be 0
//    if (memcmp (&header[24], "\0\0\0\0\0\0\0\0", 8)) {
//        trace ("bad footer\n");
//        return -1;
//    }

    uint32_t version = extract_i32_le (&header[8]);
    int32_t size = extract_i32_le (&header[12]);
    uint32_t numitems = extract_i32_le (&header[16]);
    uint32_t flags = extract_i32_le (&header[20]);

    trace ("APEv%d, size=%d, items=%d, flags=%x\n", version, size, numitems, flags);
    if (it) {
        uint32_t f = pl_get_item_flags (it);
        f |= DDB_TAG_APEV2;
        pl_set_item_flags (it, f);
    }

    // now seek to beginning of the tag (exluding header)
    if (deadbeef->fseek (fp, -size, SEEK_CUR) == -1) {
        trace ("failed to seek to tag start (-%d)\n", size);
        return -1;
    }

    int i;
    for (i = 0; i < numitems; i++) {
        uint8_t buffer[8];
        if (deadbeef->fread (buffer, 1, 8, fp) != 8) {
            return -1;
        }
        uint32_t itemsize = extract_i32_le (&buffer[0]);
        uint32_t itemflags = extract_i32_le (&buffer[4]);

        // read key until 0 (stupid and slow)
        char key[256];
        int keysize = 0;
        while (keysize <= 255) {
            if (deadbeef->fread (&key[keysize], 1, 1, fp) != 1) {
                return -1;
            }
            if (key[keysize] == 0) {
                break;
            }
            if (key[keysize] < 0x20) {
                return -1; // non-ascii chars and chars with codes 0..0x1f not allowed in ape item keys
            }
            keysize++;
        }
        key[255] = 0;
        trace ("item %d, size %d, flags %08x, keysize %d, key %s\n", i, itemsize, itemflags, keysize, key);
        // read value
        if (itemsize <= MAX_APEV2_FRAME_SIZE) // just a sanity check
        {
            uint8_t *value = malloc (itemsize+1);
            if (!value) {
                trace ("junk_read_ape_full: failed to allocate %d bytes\n", itemsize+1);
                return -1;
            }
            if (deadbeef->fread (value, 1, itemsize, fp) != itemsize) {
                trace ("junk_read_ape_full: failed to read %d bytes from file\n", itemsize);
                free (value);
                return -1;
            }
            value[itemsize] = 0;

            if ((flags&6) == 0 && strncasecmp (key, "cover art ", 10)) {
                // replace 0s with \n
                uint8_t *p = value;
                while (p < value + itemsize - 1) {
                    if (*p == 0) {
                        *p = '\n';
                    }
                    p++;
                }
            }

            junk_apev2_add_frame (it, tag_store, &tail, itemsize, itemflags, key, value);
            free (value);
        }
        else {
            // try to skip
            int err = deadbeef->fseek (fp, itemsize, SEEK_CUR);
            if (0 != err) {
                perror ("junklib: corrupted APEv2 tag\n");
                return -1;
            }
        }
    }

    return 0;
}

int
junk_apev2_read (playItem_t *it, DB_FILE *fp) {
    return junk_apev2_read_full (it, NULL, fp);
}

int
junk_apev2_read_mem (playItem_t *it, char *mem, int size) {
    return junk_apev2_read_full_mem (it, NULL, mem, size);
}

int
junk_id3v2_find (DB_FILE *fp, int *psize) {
    if (deadbeef->fseek (fp, 0, SEEK_SET) == -1) {
        trace ("junk_id3v2_find: seek error\n");
        return -1;
    }
    uint8_t header[10];
    int pos = deadbeef->ftell (fp);
    if (pos == -1) {
        trace ("junk_id3v2_find: ftell error\n");
        return -1;
    }
    if (deadbeef->fread (header, 1, 10, fp) != 10) {
        trace ("junk_id3v2_find: read error\n");
        return -1; // too short
    }
    if (strncmp (header, "ID3", 3)) {
        return -1; // no tag
    }
    uint8_t flags = header[5];
    if (flags & 15) {
        return -1; // unsupported
    }
    int footerpresent = (flags & (1<<4)) ? 1 : 0;
    // check for bad size
    if ((header[9] & 0x80) || (header[8] & 0x80) || (header[7] & 0x80) || (header[6] & 0x80)) {
        return -1; // bad header
    }
    uint32_t size = (header[9] << 0) | (header[8] << 7) | (header[7] << 14) | (header[6] << 21);
    //trace ("junklib: leading junk size %d\n", size);
    *psize = size + 10 + 10 * footerpresent;
    return pos;
}

int
junk_get_leading_size_stdio (FILE *fp) {
    uint8_t header[10];
    int pos = ftell (fp);
    if (fread (header, 1, 10, fp) != 10) {
        fseek (fp, pos, SEEK_SET);
        return -1; // too short
    }
    fseek (fp, pos, SEEK_SET);
    if (strncmp (header, "ID3", 3)) {
        return -1; // no tag
    }
    uint8_t flags = header[5];
    if (flags & 15) {
        return -1; // unsupported
    }
    int footerpresent = (flags & (1<<4)) ? 1 : 0;
    // check for bad size
    if ((header[9] & 0x80) || (header[8] & 0x80) || (header[7] & 0x80) || (header[6] & 0x80)) {
        return -1; // bad header
    }
    uint32_t size = (header[9] << 0) | (header[8] << 7) | (header[7] << 14) | (header[6] << 21);
    //trace ("junklib: leading junk size %d\n", size);
    return size + 10 + 10 * footerpresent;
}

int
junk_get_leading_size (DB_FILE *fp) {
    uint8_t header[10];
    int pos = deadbeef->ftell (fp);
    if (deadbeef->fread (header, 1, 10, fp) != 10) {
        deadbeef->fseek (fp, pos, SEEK_SET);
        trace ("junk_get_leading_size: file is too short\n");
        return -1; // too short
    }
    deadbeef->fseek (fp, pos, SEEK_SET);
    if (strncmp (header, "ID3", 3)) {
        trace ("junk_get_leading_size: no id3v2 found\n");
        return -1; // no tag
    }
    uint8_t flags = header[5];
    if (flags & 15) {
        trace ("unsupported flags in id3v2\n");
        return -1; // unsupported
    }
    int footerpresent = (flags & (1<<4)) ? 1 : 0;
    // check for bad size
    if ((header[9] & 0x80) || (header[8] & 0x80) || (header[7] & 0x80) || (header[6] & 0x80)) {
        trace ("bad header in id3v2\n");
        return -1; // bad header
    }
    uint32_t size = (header[9] << 0) | (header[8] << 7) | (header[7] << 14) | (header[6] << 21);
    //trace ("junklib: leading junk size %d\n", size);
    return size + 10 + 10 * footerpresent;
}

int
junk_id3v2_unsync (uint8_t *out, int len, int maxlen) {
    uint8_t buf [maxlen];
    uint8_t *p = buf;
    int res = -1;
    for (int i = 0; i < len; i++) {
        *p++ = out[i];
        if (i < len - 1 && out[i] == 0xff && (out[i+1] & 0xe0) == 0xe0) {
            *p++ = 0;
            res = 0;
        }
    }
    if (!res) {
        res = p-buf;
        memcpy (out, buf, res);
    }
    return res;
}

int
junk_id3v2_remove_frames (DB_id3v2_tag_t *tag, const char *frame_id) {
    DB_id3v2_frame_t *prev = NULL;
    for (DB_id3v2_frame_t *f = tag->frames; f; ) {
        DB_id3v2_frame_t *next = f->next;
        if (!strcmp (f->id, frame_id)) {
            if (prev) {
                prev->next = f->next;
            }
            else {
                tag->frames = f->next;
            }
            free (f);
        }
        else {
            prev = f;
        }
        f = next;
    }
    return 0;
}

// this function will split multiline values into separate frames
DB_id3v2_frame_t *
junk_id3v2_add_text_frame (DB_id3v2_tag_t *tag, const char *frame_id, const char *value) {
    // copy value to handle multiline strings
    size_t inlen = strlen (value);
    char buffer[inlen];
    char *pp = buffer;
    for (const char *p = value; *p; p++) {
        if (*p == '\n') {
            *pp++ = 0;
            if (tag->version[0] == 3) {
                inlen = p - value;
                break;
            }
        }
        else {
            *pp++ = *p;
        }
    }

    if (!inlen) {
        return NULL;
    }
    uint8_t out[2048];

    trace ("junklib: setting id3v2.%d text frame '%s' = '%s'\n", tag->version[0], frame_id, value);

    int encoding = 0;

    size_t outlen = -1;
    if (tag->version[0] == 4) {
        outlen = inlen;
        memcpy (out, value, inlen);
        encoding = 3;
    }
    else {
        outlen = junk_iconv (value, inlen, out, sizeof (out), UTF8_STR, "ISO-8859-1");
        if (outlen == -1) {
            outlen = junk_iconv (value, inlen, out+2, sizeof (out) - 2, UTF8_STR, "UCS-2LE");
            if (outlen <= 0) {
                return NULL;
            }
            out[0] = 0xff;
            out[1] = 0xfe;
            outlen += 2;
            trace ("successfully converted to ucs-2le (size=%d, bom: %x %x)\n", outlen, out[0], out[1]);
            encoding = 1;
        }
        else {
            trace ("successfully converted to iso8859-1 (size=%d)\n", outlen);
        }
    }

    // make a frame
    int size = outlen + 1;
    trace ("calculated frame size = %d\n", size);
    DB_id3v2_frame_t *f = malloc (size + sizeof (DB_id3v2_frame_t));
    memset (f, 0, sizeof (DB_id3v2_frame_t));
    strcpy (f->id, frame_id);
    f->size = size;
    f->data[0] = encoding;
    memcpy (f->data + 1, out, outlen);
    // append to tag
    DB_id3v2_frame_t *tail = NULL;

    for (tail = tag->frames; tail && tail->next; tail = tail->next);

    if (tail) {
        tail->next = f;
    }
    else {
        tag->frames = f;
    }
    tail = f;

    return tail;
}

DB_id3v2_frame_t *
junk_id3v2_add_comment_frame (DB_id3v2_tag_t *tag, const char *lang, const char *descr, const char *value) {
    trace ("junklib: setting 2.3 COMM frame lang=%s, descr='%s', data='%s'\n", lang, descr, value);

    // make a frame
    int descrlen = strlen (descr);
    int outlen = strlen (value);

    char input[descrlen+outlen+1];
    memcpy (input, descr, descrlen);
    input[descrlen] = 0;
    memcpy (input+descrlen+1, value, outlen);

    char buffer[2048];
    int enc = 0;
    int l;

    if (tag->version[0] == 4) {
        // utf8
        enc = 3;
        memcpy (buffer, input, sizeof (input));
        l = sizeof (input);
    }
    else {
        l = junk_iconv (input, sizeof (input), buffer, sizeof (buffer), UTF8_STR, "iso8859-1");
        if (l <= 0) {
            l = junk_iconv (input, sizeof (input), buffer+2, sizeof (buffer) - 2, UTF8_STR, "UCS-2LE");
            if (l <= 0) {
                trace ("failed to encode to ucs2 or iso8859-1\n");
                return NULL;
            }
            else {
                enc = 1;
                buffer[0] = 0xff;
                buffer[1] = 0xfe;
                l += 2;
            }
        }
    }

    trace ("calculated frame size = %d\n", l + 4);
    DB_id3v2_frame_t *f = malloc (l + 4 + sizeof (DB_id3v2_frame_t));
    memset (f, 0, sizeof (DB_id3v2_frame_t));
    strcpy (f->id, "COMM");
    // flags are all zero
    f->size = l + 4;
    f->data[0] = enc; // encoding=utf8
    memcpy (&f->data[1], lang, 3);
    memcpy (&f->data[4], buffer, l);
    // append to tag
    DB_id3v2_frame_t *tail;
    for (tail = tag->frames; tail && tail->next; tail = tail->next);
    if (tail) {
        tail->next = f;
    }
    else {
        tag->frames = f;
    }

    return f;
}

int
junk_id3v2_remove_txxx_frame (DB_id3v2_tag_t *tag, const char *key) {
    DB_id3v2_frame_t *prev = NULL;
    for (DB_id3v2_frame_t *f = tag->frames; f; ) {
        DB_id3v2_frame_t *next = f->next;
        if (!strcmp (f->id, "TXXX")) {
            char *txx = convstr_id3v2 (tag->version[0], f->data[0], f->data+1, f->size-1);
            if (txx && !strncasecmp (txx, key, strlen (key))) {
                if (prev) {
                    prev->next = f->next;
                }
                else {
                    tag->frames = f->next;
                }
                free (f);
            }
            if (txx) {
                free (txx);
            }
        }
        else {
            prev = f;
        }
        f = next;
    }
    return 0;
}

int
junk_id3v2_remove_all_txxx_frames (DB_id3v2_tag_t *tag) {
    DB_id3v2_frame_t *prev = NULL;
    for (DB_id3v2_frame_t *f = tag->frames; f; ) {
        DB_id3v2_frame_t *next = f->next;
        if (!strcmp (f->id, "TXXX")) {
            if (prev) {
                prev->next = f->next;
            }
            else {
                tag->frames = f->next;
            }
            free (f);
        }
        else {
            prev = f;
        }
        f = next;
    }
    return 0;
}

DB_id3v2_frame_t *
junk_id3v2_add_txxx_frame (DB_id3v2_tag_t *tag, const char *key, const char *value) {
    int keylen = strlen (key);
    int valuelen = strlen (value);
    int len = keylen + valuelen + 1;
    uint8_t buffer[len];
    memcpy (buffer, key, keylen);
    buffer[keylen] = 0;
    memcpy (buffer+keylen+1, value, valuelen);
    
    uint8_t out[2048];
    int encoding = 0;


    int res;

    if (tag->version[0] == 4) {
        res = len;
        encoding = 3;
        memcpy (out, key, keylen);
        out[keylen] = 0;
        memcpy (out + keylen + 1, value, valuelen);
    }
    else { // version 3
        res = junk_iconv (buffer, len, out, sizeof (out), UTF8_STR, "ISO-8859-1");
        if (res == -1) {
            res = junk_iconv (buffer, len, out+2, sizeof (out) - 2, UTF8_STR, "UCS-2LE");
            if (res == -1) {
                return NULL;
            }
            out[0] = 0xff;
            out[1] = 0xfe;
            res += 2;
            trace ("successfully converted to ucs-2le (size=%d, bom: %x %x)\n", res, out[0], out[1]);
            encoding = 1;
        }
        else {
            trace ("successfully converted to iso8859-1 (size=%d)\n", res);
        }
    }

    // make a frame
    int size = res + 1;
    trace ("calculated frame size = %d\n", size);
    DB_id3v2_frame_t *f = malloc (size + sizeof (DB_id3v2_frame_t));
    memset (f, 0, sizeof (DB_id3v2_frame_t));
    strcpy (f->id, "TXXX");
    f->size = size;
    f->data[0] = encoding;
    memcpy (&f->data[1], out, res);
    // append to tag
    DB_id3v2_frame_t *tail;
    for (tail = tag->frames; tail && tail->next; tail = tail->next);
    if (tail) {
        tail->next = f;
    }
    else {
        tag->frames = f;
    }

    return f;

}

// TODO: some non-Txxx frames might still need charset conversion
// TODO: 2.4 TDTG frame (tagging time) should not be converted, but might be useful to create it
int
junk_id3v2_convert_24_to_23 (DB_id3v2_tag_t *tag24, DB_id3v2_tag_t *tag23) {
    DB_id3v2_frame_t *f24;
    DB_id3v2_frame_t *tail = tag23->frames;
    assert (tag24->version[0] == 4);
    tag23->version[0] = 3;
    tag23->version[1] = 0;

    while (tail && tail->next) {
        tail = tail->next;
    }

    const char *copy_frames[] = {
        "AENC", "APIC",
        "COMR", "ENCR",
        "ETCO", "GEOB", "GRID",
        "LINK", "MCDI", "MLLT", "OWNE", "PRIV",
        "POPM", "POSS", "RBUF",
        "RVRB",
        "SYLT", "SYTC",
        "UFID", "USER", "USLT",
        NULL
    };

    // NOTE: 2.4 ASPI, EQU2, RVA2, SEEK, SIGN are discarded for 2.3
    // NOTE: 2.4 PCNT is discarded because it is useless
    // NOTE: all Wxxx frames are copy_frames, handled as special case


    // "TDRC" TDAT with conversion from ID3v2-strct timestamp to DDMM format
    // "TDOR" TORY with conversion from ID3v2-strct timestamp to year
    // TODO: "TIPL" IPLS with conversion to non-text format

    const char *text_frames[] = {
        "TALB", "TBPM", "TCOM", "TCON", "TCOP", "TDLY", "TENC", "TEXT", "TFLT", "TIT1", "TIT2", "TIT3", "TKEY", "TLAN", "TLEN", "TMED", "TOAL", "TOFN", "TOLY", "TOPE", "TOWN", "TPE1", "TPE2", "TPE3", "TPE4", "TPOS", "TPUB", "TRCK", "TRSN", "TRSO", "TSRC", "TSSE", "TXXX", "TDRC", NULL
    };

    // NOTE: 2.4 TMCL (musician credits list) is discarded for 2.3
    // NOTE: 2.4 TMOO (mood) is discarded for 2.3
    // NOTE: 2.4 TPRO (produced notice) is discarded for 2.3
    // NOTE: 2.4 TSOA (album sort order) is discarded for 2.3
    // NOTE: 2.4 TSOP (performer sort order) is discarded for 2.3
    // NOTE: 2.4 TSOT (title sort order) is discarded for 2.3
    // NOTE: 2.4 TSST (set subtitle) is discarded for 2.3

    for (f24 = tag24->frames; f24; f24 = f24->next) {
        DB_id3v2_frame_t *f23 = NULL;
        // we are altering the tag, so check for tag alter preservation
        if (tag24->flags & (1<<6)) {
            continue; // discard the frame
        }

        int simplecopy = 0; // means format is the same in 2.3 and 2.4
        int text = 0; // means this is a text frame

        int i;

        if (f24->id[0] == 'W') { // covers all W000..WZZZ tags
            simplecopy = 1;
        }

        if (!simplecopy) {
            for (i = 0; copy_frames[i]; i++) {
                if (!strcmp (f24->id, copy_frames[i])) {
                    simplecopy = 1;
                    break;
                }
            }
        }

        if (!simplecopy) {
            // check if this is a text frame
            for (i = 0; text_frames[i]; i++) {
                if (!strcmp (f24->id, text_frames[i])) {
                    text = 1;
                    break;
                }
            }
        }


        if (!simplecopy && !text) {
            if (!strcmp (f24->id, "COMM")) {
                uint8_t enc = f24->data[0];
                char lang[4] = {f24->data[1], f24->data[2], f24->data[3], 0};
                trace ("COMM enc is: %d\n", (int)enc);
                trace ("COMM language is: %s\n", lang);

                char *descr = convstr_id3v2 (4, enc, f24->data+4, f24->size-4);
                if (!descr) {
                    trace ("failed to decode COMM frame, probably wrong encoding (%d)\n", enc);
                }
                else {
                    // find value
                    char *value = descr;
                    while (*value && *value != '\n') {
                        value++;
                    }
                    if (*value != '\n') {
                        trace ("failed to parse COMM frame, descr was \"%s\"\n", descr);
                    }
                    else {
                        *value = 0;
                        value++;
                        f23 = junk_id3v2_add_comment_frame (tag23, lang, descr, value);
                        if (f23) {
                            tail = f23;
                            f23 = NULL;
                        }
                    }
                    free (descr);
                }
            }
            continue; // unknown frame
        }

        // convert flags
        uint8_t flags[2];
        // 1st byte (status flags) is the same, but shifted by 1 bit to the left
        flags[0] = f24->flags[0] << 1;
        
        // 2nd byte (format flags) is quite different
        // 2.4 format is %0h00kmnp (grouping, compression, encryption, unsync)
        // 2.3 format is %ijk00000 (compression, encryption, grouping)
        flags[1] = 0;
        if (f24->flags[1] & (1 << 6)) {
            flags[1] |= (1 << 4);
        }
        if (f24->flags[1] & (1 << 3)) {
            flags[1] |= (1 << 7);
        }
        if (f24->flags[1] & (1 << 2)) {
            flags[1] |= (1 << 6);
        }
        if (f24->flags[1] & (1 << 1)) {
            flags[1] |= (1 << 5);
        }
        if (f24->flags[1] & 1) {
            // 2.3 doesn't support per-frame unsyncronyzation
            // let's ignore it
        }

        if (simplecopy) {
            f23 = malloc (sizeof (DB_id3v2_frame_t) + f24->size);
            memset (f23, 0, sizeof (DB_id3v2_frame_t) + f24->size);
            strcpy (f23->id, f24->id);
            f23->size = f24->size;
            memcpy (f23->data, f24->data, f24->size);
            f23->flags[0] = flags[0];
            f23->flags[1] = flags[1];
        }
        else if (text) {

            char *decoded = convstr_id3v2 (4, f24->data[0], f24->data+1, f24->size-1);
            if (!decoded) {
                trace ("junk_id3v2_convert_24_to_23: failed to decode text frame %s\n", f24->id);
                continue; // failed, discard it
            }
            if (!strcmp (f24->id, "TDRC")) {
                trace ("junk_id3v2_convert_24_to_23: TDRC text: %s\n", decoded);
                int year, month, day;
                int c = sscanf (decoded, "%4d-%2d-%2d", &year, &month, &day);
                if (c >= 1) {
                    char s[5];
                    snprintf (s, sizeof (s), "%04d", year);
                    f23 = junk_id3v2_add_text_frame (tag23, "TYER", s);
                    if (f23) {
                        tail = f23;
                        f23 = NULL;
                    }
                }
                if (c == 3) {
                    char s[5];
                    snprintf (s, sizeof (s), "%02d%02d", month, day);
                    f23 = junk_id3v2_add_text_frame (tag23, "TDAT", s);
                    if (f23) {
                        tail = f23;
                        f23 = NULL;
                    }
                }
                else {
                    trace ("junk_id3v2_add_text_frame: 2.4 TDRC doesn't have month/day info; discarded\n");
                }
            }
            else if (!strcmp (f24->id, "TDOR")) {
                trace ("junk_id3v2_convert_24_to_23: TDOR text: %s\n", decoded);
                int year;
                int c = sscanf (decoded, "%4d", &year);
                if (c == 1) {
                    char s[5];
                    snprintf (s, sizeof (s), "%04d", year);
                    f23 = junk_id3v2_add_text_frame (tag23, "TORY", s);
                    if (f23) {
                        tail = f23;
                        f23 = NULL;
                    }
                }
                else {
                    trace ("junk_id3v2_add_text_frame: 2.4 TDOR doesn't have month/day info; discarded\n");
                }
            }
            else {
                // encode for 2.3
                f23 = junk_id3v2_add_text_frame (tag23, f24->id, decoded);
                if (f23) {
                    tail = f23;
                    f23 = NULL;
                }
            }
            free (decoded);
        }
        if (f23) {
            if (tail) {
                tail->next = f23;
            }
            else {
                tag23->frames = f23;
            }
            tail = f23;
        }
    }

    // convert tag header
    tag23->flags = tag24->flags;
    tag23->flags &= ~(1<<4); // no footer (unsupported in 2.3)
    tag23->flags &= ~(1<<7); // no unsync

    return 0;
}

int
junk_id3v2_convert_23_to_24 (DB_id3v2_tag_t *tag23, DB_id3v2_tag_t *tag24) {
    DB_id3v2_frame_t *f23;
    DB_id3v2_frame_t *tail = tag24->frames;

    while (tail && tail->next) {
        tail = tail->next;
    }

    const char *copy_frames[] = {
        "AENC", "APIC",
        "COMM", "COMR", "ENCR",
        "ETCO", "GEOB", "GRID",
        "LINK", "MCDI", "MLLT", "OWNE", "PRIV",
        "POPM", "POSS", "RBUF",
        "RVRB",
        "SYLT", "SYTC",
        "UFID", "USER", "USLT",
        NULL
    };

    // NOTE: all Wxxx frames are copy_frames, handled as special case

    // "TDRC" TDAT with conversion from ID3v2-strct timestamp to DDMM format
    // "TDOR" TORY with conversion from ID3v2-strct timestamp to year
    // TODO: "TIPL" IPLS with conversion to non-text format

    const char *text_frames[] = {
        "TALB", "TBPM", "TCOM", "TCON", "TCOP", "TDLY", "TENC", "TEXT", "TFLT", "TIT1", "TIT2", "TIT3", "TKEY", "TLAN", "TLEN", "TMED", "TOAL", "TOFN", "TOLY", "TOPE", "TOWN", "TPE1", "TPE2", "TPE3", "TPE4", "TPOS", "TPUB", "TRCK", "TRSN", "TRSO", "TSRC", "TSSE", "TXXX", "TDRC", NULL
    };

    for (f23 = tag23->frames; f23; f23 = f23->next) {
        // we are altering the tag, so check for tag alter preservation
        if (tag23->flags & (1<<7)) {
            continue; // discard the frame
        }

        int simplecopy = 0; // means format is the same in 2.3 and 2.4
        int text = 0; // means this is a text frame

        int i;

        if (f23->id[0] == 'W') { // covers all W000..WZZZ tags
            simplecopy = 1;
        }

        if (!simplecopy) {
            for (i = 0; copy_frames[i]; i++) {
                if (!strcmp (f23->id, copy_frames[i])) {
                    simplecopy = 1;
                    break;
                }
            }
        }

        if (!simplecopy) {
            // check if this is a text frame
            for (i = 0; text_frames[i]; i++) {
                if (!strcmp (f23->id, text_frames[i])) {
                    text = 1;
                    break;
                }
            }
        }


        if (!simplecopy && !text) {
            continue; // unknown frame
        }

        // convert flags
        uint8_t flags[2];
        // 1st byte (status flags) is the same, but shifted by 1 bit to the
        // right
        flags[0] = f23->flags[0] >> 1;
        
        // 2nd byte (format flags) is quite different
        // 2.4 format is %0h00kmnp (grouping, compression, encryption, unsync)
        // 2.3 format is %ijk00000 (compression, encryption, grouping)
        flags[1] = 0;
        if (f23->flags[1] & (1 << 4)) {
            flags[1] |= (1 << 6);
        }
        if (f23->flags[1] & (1 << 7)) {
            flags[1] |= (1 << 3);
        }
        if (f23->flags[1] & (1 << 6)) {
            flags[1] |= (1 << 2);
        }
        if (f23->flags[1] & (1 << 5)) {
            flags[1] |= (1 << 1);
        }

        DB_id3v2_frame_t *f24 = NULL;
        if (simplecopy) {
            f24 = malloc (sizeof (DB_id3v2_frame_t) + f23->size);
            memset (f24, 0, sizeof (DB_id3v2_frame_t) + f23->size);
            strcpy (f24->id, f23->id);
            f24->size = f23->size;
            memcpy (f24->data, f23->data, f23->size);
            f24->flags[0] = flags[0];
            f24->flags[1] = flags[1];
        }
        else if (text) {
            // decode text into utf8
            char *decoded = convstr_id3v2 (3, f23->data[0], f23->data+1, f23->size-1);
            if (!decoded) {
                trace ("junk_id3v2_convert_23_to_24: failed to decode text frame %s\n", f23->id);
                continue; // failed, discard it
            }
            if (!strcmp (f23->id, "TDRC")) {
                trace ("junk_id3v2_convert_23_to_24: TDRC text: %s\n", decoded);
                int year, month, day;
                int c = sscanf (decoded, "%4d-%2d-%2d", &year, &month, &day);
                if (c >= 1) {
                    char s[5];
                    snprintf (s, sizeof (s), "%04d", year);
                    f24 = junk_id3v2_add_text_frame (tag24, "TYER", s);
                    if (f24) {
                        tail = f24;
                        f24 = NULL;
                    }
                }
                if (c == 3) {
                    char s[5];
                    snprintf (s, sizeof (s), "%02d%02d", month, day);
                    f24 = junk_id3v2_add_text_frame (tag24, "TDAT", s);
                    if (f24) {
                        tail = f24;
                        f24 = NULL;
                    }
                }
                else {
                    trace ("junk_id3v2_add_text_frame: 2.4 TDRC doesn't have month/day info; discarded\n");
                }
            }
            else if (!strcmp (f23->id, "TORY")) {
                trace ("junk_id3v2_convert_23_to_24: TDOR text: %s\n", decoded);
                int year;
                int c = sscanf (decoded, "%4d", &year);
                if (c == 1) {
                    char s[5];
                    snprintf (s, sizeof (s), "%04d", year);
                    f24 = junk_id3v2_add_text_frame (tag24, "TDOR", s);
                    if (f24) {
                        tail = f24;
                        f24 = NULL;
                    }
                }
                else {
                    trace ("junk_id3v2_add_text_frame: 2.4 TDOR doesn't have month/day info; discarded\n");
                }
            }
            else {
                // encode for 2.4
                f24 = junk_id3v2_add_text_frame (tag24, f23->id, decoded);
                if (f24) {
                    tail = f24;
                    f24 = NULL;
                }
            }
            free (decoded);
        }
        if (f24) {
            if (tail) {
                tail->next = f24;
            }
            else {
                tag24->frames = f24;
            }
            tail = f24;
        }
    }

    // convert tag header
    tag24->version[0] = 4;
    tag24->version[1] = 0;
    tag24->flags = tag23->flags;
    tag24->flags &= ~(1<<4); // no footer (unsupported in 2.3)
    tag24->flags &= ~(1<<7); // no unsync

    return 0;
}

int
junk_id3v2_convert_22_to_24 (DB_id3v2_tag_t *tag22, DB_id3v2_tag_t *tag24) {
    DB_id3v2_frame_t *f22;
    DB_id3v2_frame_t *tail = tag24->frames;

    while (tail && tail->next) {
        tail = tail->next;
    }

    const char *copy_frames[] = {
        "BUF", "COM", "CRA", "ETC", "GEO", "IPL", "MCI", "MLL", "POP", "REV", "SLT", "STC", "UFI", "ULT",
        NULL
    };

    const char *copy_frames_24[] = {
        "RBUF", "COMM", "AENC", "ETCO", "GEOB", "TIPL", "MCDI", "MLLT", "POPM", "RVRB", "SYLT", "SYTC", "UFID", "USLT",
        NULL
    };

    // NOTE: BUF is discarded (no match in 2.4)
    // NOTE: CNT is discarded (useless)
    // NOTE: CRM is discarded (no match in 2.4)
    // NOTE: EQU is discarded (difficult to convert to EQU2)
    // NOTE: LNK is discarded (maybe later)
    // NOTE: PIC is discarded (needs conversion from custom image-format field to mime-type)
    // NOTE: RVA is discarded (subjective, and difficult to convert to RVA2)

    const char *text_frames[] = {
        "TAL", "TBP", "TCM", "TCO", "TCR", "TDY", "TEN", "TFT", "TKE", "TLA", "TLE", "TMT", "TOA", "TOF", "TOL", "TOT", "TP1", "TP2", "TP3", "TP4", "TPA", "TPB", "TRC", "TRK", "TSS", "TT1", "TT2", "TT3", "TXT", "TXX", "TOR", NULL
    };

    const char *text_frames_24[] = {
        "TALB", "TBPM", "TCOM", "TCON", "TCOP", "TDLY", "TENC", "TFLT", "TKEY", "TLAN", "TLEN", "TMED", "TOPE", "TOFN", "TOLY", "TOAL", "TPE1", "TPE2", "TPE3", "TPE4", "TPOS", "TPUB", "TSRC", "TRCK", "TSSE", "TIT1", "TIT2", "TIT3", "TEXT", "TXXX", "TDOR"
    };

    // NOTE: TRD is discarded (no match in 2.4)
    // NOTE: TSI is discarded (no match in 2.4)

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;

    for (f22 = tag22->frames; f22; f22 = f22->next) {
        int simplecopy = -1; // means format is the same in 2.2 and 2.4
        int text = -1; // means this is a text frame

        int i;

        if (f22->id[0] == 'W') { // covers all W00..WZZ tags
            simplecopy = 0;
        }

        if (simplecopy == -1) {
            for (i = 0; copy_frames[i]; i++) {
                if (!strcmp (f22->id, copy_frames[i])) {
                    simplecopy = i;
                    break;
                }
            }
        }

        if (simplecopy == -1) {
            // check if this is a text frame
            for (i = 0; text_frames[i]; i++) {
                if (!strcmp (f22->id, text_frames[i])) {
                    text = i;
                    break;
                }
            }
        }


        if (simplecopy == -1 && text == -1) {
            continue; // unknown frame
        }

        // convert flags
        uint8_t flags[2];
        // 1st byte (status flags) is the same, but shifted by 1 bit to the
        // right
        flags[0] = f22->flags[0] >> 1;
        
        // 2nd byte (format flags) is quite different
        // 2.4 format is %0h00kmnp (grouping, compression, encryption, unsync)
        // 2.3 format is %ijk00000 (compression, encryption, grouping)
        flags[1] = 0;
        if (f22->flags[1] & (1 << 4)) {
            flags[1] |= (1 << 6);
        }
        if (f22->flags[1] & (1 << 7)) {
            flags[1] |= (1 << 3);
        }
        if (f22->flags[1] & (1 << 6)) {
            flags[1] |= (1 << 2);
        }
        if (f22->flags[1] & (1 << 5)) {
            flags[1] |= (1 << 1);
        }

        DB_id3v2_frame_t *f24 = NULL;
        if (simplecopy != -1) {
            f24 = malloc (sizeof (DB_id3v2_frame_t) + f22->size);
            memset (f24, 0, sizeof (DB_id3v2_frame_t) + f22->size);
            if (f22->id[0] == 'W') { // duplicate last letter of W00-WZZ frames
                strcpy (f24->id, f22->id);
                f24->id[3] = f24->id[2];
                f24->id[4] = 0;
            }
            else {
                strcpy (f24->id, copy_frames_24[simplecopy]);
            }
            f24->size = f22->size;
            memcpy (f24->data, f22->data, f22->size);
            f24->flags[0] = flags[0];
            f24->flags[1] = flags[1];
        }
        else if (text != -1) {
            // decode text into utf8
            char *decoded = convstr_id3v2 (2, f22->data[0], f22->data+1, f22->size-1);
            if (!decoded) {
                trace ("junk_id3v2_convert_23_to_24: failed to decode text frame %s\n", f22->id);
                continue; // failed, discard it
            }
            // encode for 2.4
            f24 = junk_id3v2_add_text_frame (tag24, text_frames_24[text], decoded);
            if (f24) {
                tail = f24;
                f24 = NULL;
            }
            free (decoded);
        }
        else if (!strcmp (f22->id, "TYE")) {
            char *decoded = convstr_id3v2 (2, f22->data[0], f22->data+1, f22->size-1);
            if (decoded) {
                year = atoi (decoded);
                free (decoded);
            }
        }
        else if (!strcmp (f22->id, "TDA")) {
            char *decoded = convstr_id3v2 (2, f22->data[0], f22->data+1, f22->size-1);
            if (decoded) {
                sscanf (decoded, "%02d%02d", &month, &day);
                free (decoded);
            }
        }
        else if (!strcmp (f22->id, "TIM")) {
            char *decoded = convstr_id3v2 (2, f22->data[0], f22->data+1, f22->size-1);
            if (decoded) {
                sscanf (decoded, "%02d%02d", &hour, &minute);
                free (decoded);
            }
        }
        if (f24) {
            if (tail) {
                tail->next = f24;
            }
            else {
                tag24->frames = f24;
            }
            tail = f24;
        }
    }

    char tdrc[100];
    char *p = tdrc;
    if (year > 0) {
        int n = sprintf (p, "%04d", year);
        p += n;
        if (month) {
            n = sprintf (p, "-%02d", month);
            p += n;
            if (day) {
                n = sprintf (p, "-%02d", day);
                p += n;
                if (hour && minute) {
                    n = sprintf (p, "-T%02d:%02d", hour, minute);
                    p += n;
                }
            }
        }
        DB_id3v2_frame_t *f24 = junk_id3v2_add_text_frame (tag24, "TDRC", tdrc);
        if (f24) {
            tail = f24;
        }
    }

    // convert tag header
    tag24->version[0] = 4;
    tag24->version[1] = 0;
    tag24->flags = tag22->flags;
    tag24->flags &= ~(1<<4); // no footer (unsupported in 2.3)
    tag24->flags &= ~(1<<7); // no unsync

    return 0;
}

int
junk_apev2_remove_frames (DB_apev2_tag_t *tag, const char *frame_id) {
    DB_apev2_frame_t *prev = NULL;
    for (DB_apev2_frame_t *f = tag->frames; f; ) {
        DB_apev2_frame_t *next = f->next;
        if (!strcasecmp (f->key, frame_id)) {
            if (prev) {
                prev->next = f->next;
            }
            else {
                tag->frames = f->next;
            }
            free (f);
        }
        else {
            prev = f;
        }
        f = next;
    }
    return 0;
}

int
junk_apev2_remove_all_text_frames (DB_apev2_tag_t *tag) {
    DB_apev2_frame_t *prev = NULL;
    for (DB_apev2_frame_t *f = tag->frames; f; ) {
        DB_apev2_frame_t *next = f->next;
        int valuetype = ((f->flags >> 1) & 3);
        if (valuetype == 0) {
            if (prev) {
                prev->next = f->next;
            }
            else {
                tag->frames = f->next;
            }
            free (f);
        }
        else {
            prev = f;
        }
        f = next;
    }
    return 0;
}
DB_apev2_frame_t *
junk_apev2_add_text_frame (DB_apev2_tag_t *tag, const char *frame_id, const char *value) {
    trace ("adding apev2 frame %s %s\n", frame_id, value);
    if (!*value) {
        return NULL;
    }
    DB_apev2_frame_t *tail = tag->frames;
    while (tail && tail->next) {
        tail = tail->next;
    }

#if 0 // this will split every line into separate field
    const char *next = value;
    while (*value) {
        while (*next && *next != '\n') {
            next++;
        }

        //int size = strlen (value);
        int size = next - value;
        if (*next) {
            next++;
        }

        if (!size) {
            continue;
        }

        trace ("adding apev2 subframe %s len %d\n", value, size);
        DB_apev2_frame_t *f = malloc (sizeof (DB_apev2_frame_t) + size);
        if (!f) {
            trace ("junk_apev2_add_text_frame: failed to allocate %d bytes\n", size);
            return NULL;
        }
        memset (f, 0, sizeof (DB_apev2_frame_t));
        f->flags = 0;
        strcpy (f->key, frame_id);
        f->size = size;
        memcpy (f->data, value, size);

        if (tail) {
            tail->next = f;
        }
        else {
            tag->frames = f;
        }
        tail = f;

        value = next;
    }
#endif
    size_t size = strlen (value);
    DB_apev2_frame_t *f = malloc (sizeof (DB_apev2_frame_t) + size);
    if (!f) {
        trace ("junk_apev2_add_text_frame: failed to allocate %d bytes\n", size);
        return NULL;
    }
    memset (f, 0, sizeof (DB_apev2_frame_t));
    f->flags = 0;
    strcpy (f->key, frame_id);
    f->size = size;
    memcpy (f->data, value, size);

    if (tail) {
        tail->next = f;
    }
    else {
        tag->frames = f;
    }
    tail = f;
    return tail;
}

int
junk_id3v2_convert_apev2_to_24 (DB_apev2_tag_t *ape, DB_id3v2_tag_t *tag24) {
    DB_apev2_frame_t *f_ape;
    DB_id3v2_frame_t *tail = tag24->frames;

    while (tail && tail->next) {
        tail = tail->next;
    }

    const char *text_keys[] = {
        "Title", "Subtitle", "Artist", "Album", "Publisher", "Conductor", "Track", "Composer", "Copyright", "Genre", "Disc", "ISRC", "Language", "Year", NULL
    };

    const char *text_keys_24[] = {
        "TIT2", "TIT3", "TPE1", "TALB", "TPUB", "TPE3", "TRCK", "TCOM", "TCOP", "TCON", "TPOS", "TSRC", "TLAN", "TDRC"
    };

    const char *comm_frames[] = {
        "Comment", "EAN/UPC", "ISBN", "Catalog", "LC", "Publicationright", "Record Location", "Related", "Abstract", "Bibliography", NULL
    };

    // FIXME: additional frames: File->WOAF 
    // converted to COMM: Comment, EAN/UPC, ISBN, Catalog, LC, Publicationright, Record Location, Related, Abstract, Bibliography
    // "Debut album" is discarded
    // "Index" is discarded
    // "Introplay" is discarded

    for (f_ape = ape->frames; f_ape; f_ape = f_ape->next) {
        int i;

        for (i = 0; text_keys[i]; i++) {
            if (!strcasecmp (text_keys[i], f_ape->key)) {
                break;
            }
        }

        DB_id3v2_frame_t *f24 = NULL;

        if (text_keys[i]) {
            char str[f_ape->size+1];
            memcpy (str, f_ape->data, f_ape->size);
            str[f_ape->size] = 0;
            f24 = junk_id3v2_add_text_frame (tag24, text_keys_24[i], str);
            if (f24) {
                tail = f24;
                f24 = NULL;
            }
        }
        else {
            for (i = 0; comm_frames[i]; i++) {
                if (!strcasecmp (f_ape->key, comm_frames[i])) {
                    char str[f_ape->size+1];
                    memcpy (str, f_ape->data, f_ape->size);
                    str[f_ape->size] = 0;
                    if (!strcasecmp (f_ape->key, "Comment")) {
                        junk_id3v2_add_comment_frame (tag24, "eng", "", str);
                    }
                    else {
                        junk_id3v2_add_comment_frame (tag24, "eng", comm_frames[i], str);
                    }
                    break;
                }
            }
        }

        if (f24) {
            if (tail) {
                tail->next = f24;
            }
            else {
                tag24->frames = f24;
            }
            tail = f24;
        }
    }

    // convert tag header
    tag24->version[0] = 4;
    tag24->version[1] = 0;
    tag24->flags = 0;

    return 0;
}

int
junk_apev2_write_i32_le (FILE *fp, uint32_t data) {
    int shift = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t d = (data >> shift) & 0xff;
        if (fwrite (&d, 1, 1, fp) != 1) {
            return -1;
        }
        shift += 8;
    }

    return 0;
}

int
junk_apev2_write (FILE *fp, DB_apev2_tag_t *tag, int write_header, int write_footer) {
    // calc size and numitems
    uint32_t numframes = 0;
    uint32_t size = 0;
    DB_apev2_frame_t *f = tag->frames;
    while (f) {
        size += 8 + strlen (f->key) + 1 + f->size;
        numframes++;
        f = f->next;
    }
    size += 32;

    trace ("junk_apev2_write: writing apev2 tag, size=%d, numframes=%d\n", size, numframes);


    if (write_header) {
        if (fwrite ("APETAGEX", 1, 8, fp) != 8) {
            trace ("junk_apev2_write: failed to write apev2 header signature\n");
            goto error;
        }
        uint32_t flags = (1 << 31) | (1 << 29); // contains header, this is header
        if (!write_footer) {
            flags |= 1 << 30; // contains no footer
        }
        uint32_t header[4] = {
            2000, // version
            size,
            numframes,
            flags
        };
        for (int i = 0; i < 4; i++) {
            if (junk_apev2_write_i32_le (fp, header[i]) != 0) {
                trace ("junk_apev2_write_i32_le: failed to write apev2 header\n");
                goto error;
            }
        }
        // write 8 bytes of padding
        header[0] = header[1] = 0;
        if (fwrite (header, 1, 8, fp) != 8) {
            trace ("junk_apev2_write_i32_le: failed to write apev2 header padding\n");
            goto error;
        }
    }

    // write items
    f = tag->frames;
    while (f) {
        if (junk_apev2_write_i32_le (fp, f->size) != 0) {
            trace ("junk_apev2_write_i32_le: failed to write apev2 item size\n");
            goto error;
        }
        if (junk_apev2_write_i32_le (fp, f->flags) != 0) {
            trace ("junk_apev2_write_i32_le: failed to write apev2 item flags\n");
            goto error;
        }
        int l = strlen (f->key) + 1;
        if (fwrite (f->key, 1, l, fp) != l) {
            trace ("junk_apev2_write_i32_le: failed to write apev2 item key\n");
            goto error;
        }
        if (fwrite (f->data, 1, f->size, fp) != f->size) {
            trace ("junk_apev2_write_i32_le: failed to write apev2 item value\n");
            goto error;
        }
        f = f->next;
    }

    if (write_footer) {
        if (fwrite ("APETAGEX", 1, 8, fp) != 8) {
            trace ("junk_apev2_write: failed to write apev2 footer signature\n");
            goto error;
        }
        uint32_t flags = 0;
        if (write_header) {
            flags |= 1 << 31;
        }
        uint32_t header[4] = {
            2000, // version
            size,
            numframes,
            flags
        };
        for (int i = 0; i < 4; i++) {
            if (junk_apev2_write_i32_le (fp, header[i]) != 0) {
                trace ("junk_apev2_write_i32_le: failed to write apev2 footer\n");
                goto error;
            }
        }
        // write 8 bytes of padding
        header[0] = header[1] = 0;
        if (fwrite (header, 1, 8, fp) != 8) {
            trace ("junk_apev2_write_i32_le: failed to write apev2 footer padding\n");
            goto error;
        }
    }
    return 0;
error:
    return -1;
}

int
junk_id3v2_write (FILE *out, DB_id3v2_tag_t *tag) {
    if (tag->version[0] < 3) {
        fprintf (stderr, "junk_write_id3v2: writing id3v2.2 is not supported\n");
        return -1;
    }

    FILE *fp = NULL;
    char *buffer = NULL;
    int err = -1;

    // write tag header
    if (fwrite ("ID3", 1, 3, out) != 3) {
        fprintf (stderr, "junk_write_id3v2: failed to write ID3 signature\n");
        goto error;
    }

    if (fwrite (tag->version, 1, 2, out) != 2) {
        fprintf (stderr, "junk_write_id3v2: failed to write tag version\n");
        goto error;
    }
    uint8_t flags = tag->flags;
    flags &= ~(1<<6); // we don't (yet?) write ext header
    flags &= ~(1<<4); // we don't write footer

    if (fwrite (&flags, 1, 1, out) != 1) {
        fprintf (stderr, "junk_write_id3v2: failed to write tag flags\n");
        goto error;
    }
    // run through list of frames, and calculate size
    uint32_t sz = 0;
    for (DB_id3v2_frame_t *f = tag->frames; f; f = f->next) {
        // each tag has 10 bytes header
        if (tag->version[0] > 2) {
            sz += 10;
        }
        else {
            sz += 6;
        }
        sz += f->size;
    }

    trace ("calculated tag size: %d bytes\n", sz);
    uint8_t tagsize[4];
    tagsize[0] = (sz >> 21) & 0x7f;
    tagsize[1] = (sz >> 14) & 0x7f;
    tagsize[2] = (sz >> 7) & 0x7f;
    tagsize[3] = sz & 0x7f;
    if (fwrite (tagsize, 1, 4, out) != 4) {
        fprintf (stderr, "junk_write_id3v2: failed to write tag size\n");
        goto error;
    }

    trace ("writing frames\n");
    // write frames
    for (DB_id3v2_frame_t *f = tag->frames; f; f = f->next) {
        trace ("writing frame %s size %d\n", f->id, f->size);
        int id_size = 3;
        uint8_t frame_size[4];
        if (tag->version[0] > 2) {
            id_size = 4;
        }
        if (tag->version[0] == 3) {
            frame_size[0] = (f->size >> 24) & 0xff;
            frame_size[1] = (f->size >> 16) & 0xff;
            frame_size[2] = (f->size >> 8) & 0xff;
            frame_size[3] = f->size & 0xff;
        }
        else if (tag->version[0] == 4) {
            frame_size[0] = (f->size >> 21) & 0x7f;
            frame_size[1] = (f->size >> 14) & 0x7f;
            frame_size[2] = (f->size >> 7) & 0x7f;
            frame_size[3] = f->size & 0x7f;
        }
        if (fwrite (f->id, 1, 4, out) != 4) {
            fprintf (stderr, "junk_write_id3v2: failed to write frame id %s\n", f->id);
            goto error;
        }
        if (fwrite (frame_size, 1, 4, out) != 4) {
            fprintf (stderr, "junk_write_id3v2: failed to write frame size, id %s, size %d\n", f->id, f->size);
            goto error;
        }
        if (fwrite (f->flags, 1, 2, out) != 2) {
            fprintf (stderr, "junk_write_id3v2: failed to write frame header flags, id %s, size %d\n", f->id, f->size);
            goto error;
        }
        if (fwrite (f->data, 1, f->size, out) != f->size) {
            fprintf (stderr, "junk_write_id3v2: failed to write frame data, id %s, size %d\n", f->id, f->size);
            goto error;
        }
        sz += f->size;
    }

    return 0;

error:
    if (buffer) {
        free (buffer);
    }
    return err;
}

void
junk_id3v2_free (DB_id3v2_tag_t *tag) {
    while (tag->frames) {
        DB_id3v2_frame_t *next = tag->frames->next;
        free (tag->frames);
        tag->frames = next;
    }
}

void
junk_apev2_free (DB_apev2_tag_t *tag) {
    while (tag->frames) {
        DB_apev2_frame_t *next = tag->frames->next;
        free (tag->frames);
        tag->frames = next;
    }
}

int
junklib_id3v2_sync_frame (uint8_t *data, int size) {
    char *writeptr = data;
    int skipnext = 0;
    int written = 0;
    while (size > 0) {
        *writeptr = *data;
        if (data[0] == 0xff && size >= 2 && data[1] == 0) {
            data++;
            size--;
        }
        writeptr++;
        data++;
        size--;
        written++;
    }
    return written;
}

char *
junk_append_meta (const char *old, const char *new) {
    int sz = strlen (old) + strlen (new) + 2;
    char *appended = malloc (sz);
    if (!appended) {
        trace ("junk_append_meta: failed to allocate %d bytes\n");
        return NULL;
    }
    snprintf (appended, sz, "%s\n%s", old, new);
    return appended;
}

int
junk_load_comm_frame (int version_major, playItem_t *it, uint8_t *readptr, int synched_size) {
    uint8_t enc = readptr[0];
    char lang[4] = {readptr[1], readptr[2], readptr[3], 0};
    trace ("COMM enc: %d\n", (int)enc);
    trace ("COMM language: %s\n", lang);
    trace ("COMM data size: %d\n", synched_size);

    char *descr = convstr_id3v2 (version_major, enc, readptr+4, synched_size-4);
    if (!descr) {
        trace ("failed to decode COMM frame, probably wrong encoding (%d)\n", enc);
        return -1;
    }

    trace ("COMM raw data: %s\n", descr);
    // find value
    char *value = descr;
    while (*value && *value != '\n') {
        value++;
    }
    if (*value != '\n') {
        trace ("failed to parse COMM frame, descr was \"%s\"\n", descr);
        free (descr);
        return -1;
    }

    *value = 0;
    value++;

    int len = strlen (descr) + strlen (value) + 3;
    char comment[len];

    if (*descr) {
        snprintf (comment, len, "%s: %s", descr, value);
    }
    else {
        strcpy (comment, value);
    }

    trace ("COMM combined: %s\n", comment);
    // skip utf8 BOM (can be produced by iconv FEFF/FFFE)
    int l = strlen (comment);
    uint8_t bom[] = { 0xEF, 0xBB, 0xBF };
    if (l >= 3 && !memcmp (comment, bom, 3)) {
        pl_append_meta (it, "comment", comment+3);
    }
    else {
        pl_append_meta (it, "comment", comment);
    }

    free (descr);
    return 0;
}

/* Parse RVA2 tag */
/* Currently only supports tags wich set master volume and are labeled "track"
 * or "album". Also only supports peak value if stored as 16 bits. */
static int junk_id3v2_load_rva2 (int version_major, playItem_t *it, uint8_t *readptr, int synched_size) {
    char *rva_desc = readptr;
    unsigned rva_desc_len = 0;
    const char *p = rva_desc;
    while (*p++ && rva_desc_len < synched_size) {
        rva_desc_len++;
    }

    if(rva_desc_len == synched_size) { /* tag too short */
        return -1;
    }
    if(rva_desc_len != 5) { /* only support track or album labeled ones */
        return 0;
    }

    if(synched_size < rva_desc_len + 1 + 4) return -1; /* at least 4 bytes after zero-terminated label */

    uint8_t *rva_data = rva_desc + rva_desc_len + 1;

    uint8_t vol_type = rva_data[0];

    if(vol_type != 1) return 0;

    int16_t volume_adjust = (int16_t)(((int16_t)rva_data[1] << 8) | rva_data[2]); /* this is little-endian safe :) */
    uint8_t peak_bits = rva_data[3];
    uint16_t peak_val = 0;

    if(peak_bits == 16 && synched_size >= rva_desc_len + 1 + 6) {
        peak_val = (uint16_t)((rva_data[4] << 8) | rva_data[5]);
    }

    if (!strcasecmp (rva_desc, "album")) {
        pl_set_item_replaygain (it, DDB_REPLAYGAIN_ALBUMGAIN, (float)volume_adjust / 512.0);
        if(peak_val) pl_set_item_replaygain (it, DDB_REPLAYGAIN_ALBUMPEAK, (float)peak_val / 32767.0); /* NOTE: this is a guess based on mp3gain 1.5.2 written tags */
    }
    else if (!strcasecmp (rva_desc, "track")) {
        pl_set_item_replaygain (it, DDB_REPLAYGAIN_TRACKGAIN, (float)volume_adjust / 512.0);
        if(peak_val) pl_set_item_replaygain (it, DDB_REPLAYGAIN_TRACKPEAK, (float)peak_val / 32767.0);
    }

    return 0;
}

int
junk_id3v2_load_txx (int version_major, playItem_t *it, uint8_t *readptr, int synched_size) {
    char *txx = convstr_id3v2 (version_major, *readptr, readptr+1, synched_size-1);
    if (!txx) {
        return -1;
    }

    char *val = NULL;
    if (txx) {
        char *p;
        for (p = txx; *p; p++) {
            if (*p == '\n') {
                *p = 0;
                val = p+1;
                break;
            }
        }
    }

    if (val) {
        // skip utf8 BOM (can be produced by iconv FEFF/FFFE)
        int l = strlen (val);
        uint8_t bom[] = { 0xEF, 0xBB, 0xBF };
        if (l >= 3 && !memcmp (val, bom, 3)) {
            val += 3;
        }

        if (!strcasecmp (txx, "replaygain_album_gain")) {
            pl_set_item_replaygain (it, DDB_REPLAYGAIN_ALBUMGAIN, atof (val));
        }
        else if (!strcasecmp (txx, "replaygain_album_peak")) {
            pl_set_item_replaygain (it, DDB_REPLAYGAIN_ALBUMPEAK, atof (val));
        }
        else if (!strcasecmp (txx, "replaygain_track_gain")) {
            pl_set_item_replaygain (it, DDB_REPLAYGAIN_TRACKGAIN, atof (val));
        }
        else if (!strcasecmp (txx, "replaygain_track_peak")) {
            pl_set_item_replaygain (it, DDB_REPLAYGAIN_TRACKPEAK, atof (val));
        }
        else if (!strcasecmp (txx, "date")) { // HACK: fb2k date support
            pl_append_meta (it, "year", val);
        }
        else {
            pl_append_meta (it, txx, val);
        }
    }

    free (txx);

    return 0;
}

int
junk_id3v2_add_genre (playItem_t *it, char *genre) {
    int numeric = 0;
    if (genre[0] == '(') {
        // find matching parenthesis
        char *p = &genre[1];
        while (*p && *p != ')') {
            if (!isdigit (*p)) {
                break;
            }
            p++;
        }
        if (*p == ')') {
            *p = 0;
            memmove (genre, genre+1, p-genre);
            numeric = 1;
        }
    }
    if (!numeric) {
        // check if it is numeric
        const char *p = genre;
        while (*p) {
            if (!isdigit (*p)) {
                break;
            }
            p++;
        }
        if (*p == 0 && p > genre) {
            numeric = 1;
        }
    }

    if (numeric) {
        int genre_id = atoi (genre);
        if (genre_id >= 0) {
            const char *genre_str = NULL;
            if (genre_id <= 147) {
                genre_str = junk_genretbl[genre_id];
            }
            else if (genre_id == 0xff) {
                // genre_str = "None";
            }
            if (genre_str) {
                pl_add_meta (it, "genre", genre_str);
                return 0;
            }
        }
    }
    else if (!strcmp (genre, "CR")) {
        pl_add_meta (it, "genre", "Cover");
    }
    else if (!strcmp (genre, "RX")) {
        pl_add_meta (it, "genre", "Remix");
    }
    else {
        pl_add_meta (it, "genre", genre);
    }

    return 0;
}

int
junk_id3v2_read_full (playItem_t *it, DB_id3v2_tag_t *tag_store, DB_FILE *fp) {
    DB_id3v2_frame_t *tail = NULL;
    int title_added = 0;
    if (!fp) {
        trace ("bad call to junk_id3v2_read!\n");
        return -1;
    }
    deadbeef->rewind (fp);
    uint8_t header[10];
    if (deadbeef->fread (header, 1, 10, fp) != 10) {
        return -1; // too short
    }
    if (strncmp (header, "ID3", 3)) {
        return -1; // no tag
    }
    uint8_t version_major = header[3];
    uint8_t version_minor = header[4];
    if (version_major > 4 || version_major < 2) {
        trace ("id3v2.%d.%d is unsupported\n", version_major, version_minor);
        return -1; // unsupported
    }
    uint8_t flags = header[5];
    if (flags & 15) {
        trace ("unrecognized flags: one of low 15 bits is set, value=0x%x\n", (int)flags);
        return -1; // unsupported
    }
    int unsync = (flags & (1<<7)) ? 1 : 0;
    int extheader = (flags & (1<<6)) ? 1 : 0;
    int expindicator = (flags & (1<<5)) ? 1 : 0;
    int footerpresent = (flags & (1<<4)) ? 1 : 0;
    // check for bad size
    if ((header[9] & 0x80) || (header[8] & 0x80) || (header[7] & 0x80) || (header[6] & 0x80)) {
        trace ("bad header size\n");
        return -1; // bad header
    }
    uint32_t size = (header[9] << 0) | (header[8] << 7) | (header[7] << 14) | (header[6] << 21);

    trace ("tag size: %d\n", size);
    if (size == 0) {
        return -1;
    }
    if (tag_store) {
        tag_store->version[0] = version_major;
        tag_store->version[1] = version_minor;
        tag_store->flags = flags;
        // remove unsync flag
        tag_store->flags &= ~ (1<<7);
    }

    uint8_t *tag = malloc (size);
    if (!tag) {
        fprintf (stderr, "junklib: out of memory while reading id3v2, tried to alloc %d bytes\n", size);
        goto error;
    }
    if (deadbeef->fread (tag, 1, size, fp) != size) {
        goto error; // bad size
    }
    uint8_t *readptr = tag;
    int crcpresent = 0;
    trace ("version: 2.%d.%d, unsync: %d, extheader: %d, experimental: %d\n", version_major, version_minor, unsync, extheader, expindicator);
    
    if (extheader) {
        uint32_t sz = (readptr[3] << 0) | (readptr[2] << 7) | (readptr[1] << 14) | (readptr[0] << 21);
        if (size < sz) {
            trace ("error: size of ext header (%d) is greater than tag size\n", sz);
            goto error; // bad size
        }
        readptr += sz;
    }
    int err = -1;
    while (readptr - tag <= size - 4 && *readptr) {
        if (version_major == 3 || version_major == 4) {
            trace ("pos %d of %d\n", readptr - tag, size);
            char frameid[5];
            memcpy (frameid, readptr, 4);
            frameid[4] = 0;
            readptr += 4;
            if (readptr - tag >= size - 4) {
                trace ("reached the end of tag\n");
                break;
            }
            uint32_t sz;
            if (version_major == 4) {
                sz = (readptr[3] << 0) | (readptr[2] << 7) | (readptr[1] << 14) | (readptr[0] << 21);
            }
            else if (version_major == 3) {
                sz = (readptr[3] << 0) | (readptr[2] << 8) | (readptr[1] << 16) | (readptr[0] << 24);
            }
            else {
                trace ("unknown id3v2 version (2.%d.%d)\n", version_major, version_minor);
                goto error;
            }
            readptr += 4;
            trace ("got frame %s, size %d, pos %d, tagsize %d\n", frameid, sz, readptr-tag, size);
            if (readptr - tag >= size - sz) {
                trace ("frame is out of tag bounds\n");
                err = -1;
                goto error; // size of frame is more than size of tag
            }
            if (sz < 1) {
//                err = 1;
                break; // frame must be at least 1 byte long
            }
            uint8_t flags1 = readptr[0];
            uint8_t flags2 = readptr[1];
            readptr += 2;

            if (!strcmp (frameid, "APIC")) {
                if (sz > MAX_ID3V2_APIC_FRAME_SIZE) {
                    trace ("junk_id3v2_read_full: frame %s size is too big (%d), discarded\n", frameid, sz);
                    readptr += sz;
                    continue;
                }
            }
            else if (sz > MAX_ID3V2_FRAME_SIZE || readptr - tag + sz > size) {
                trace ("junk_id3v2_read_full: frame %s size is too big (%d), discarded\n", frameid, sz);
                readptr += sz;
                continue;
            }
            int synched_size = sz;
            if (unsync) {
                synched_size = junklib_id3v2_sync_frame (readptr, sz);
                trace ("size: %d/%d\n", synched_size, sz);

#if 0 // that was a workaround for corrupted unsynced tag, do not use
                if (synched_size != sz) {
                    sz = sz + (sz-synched_size);
                }
#endif
            }

            if (tag_store) {
                DB_id3v2_frame_t *frm = malloc (sizeof (DB_id3v2_frame_t) + sz);
                if (!frm) {
                    fprintf (stderr, "junklib: failed to alloc %d bytes for id3v2 frame %s\n", (int)(sizeof (DB_id3v2_frame_t) + sz), frameid);
                    goto error;
                }
                memset (frm, 0, sizeof (DB_id3v2_frame_t));
                if (tail) {
                    tail->next = frm;
                }
                tail = frm;
                if (!tag_store->frames) {
                    tag_store->frames = frm;
                }
                strcpy (frm->id, frameid);
                memcpy (frm->data, readptr, sz);
                frm->size = synched_size;

                frm->flags[0] = flags1;
                frm->flags[1] = flags2;
            }
            if (version_major == 4) {
                if (flags1 & 0x8f) {
                    // unknown flags
                    trace ("unknown status flags: %02x\n", flags1);
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0xb0) {
                    // unknown flags
                    trace ("unknown format flags: %02x\n", flags2);
                    readptr += sz;
                    continue;
                }

                if (flags2 & 0x40) { // group id
                    trace ("frame has group id\n");
                    readptr++; // skip id
                    sz--;
                }
                if (flags2 & 0x08) { // compressed frame, ignore
                    trace ("frame is compressed, skipping\n");
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x04) { // encrypted frame, skip
                    trace ("frame is encrypted, skipping\n");
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x02) { // unsync, just do nothing
                }
                if (flags2 & 0x01) { // data size
                    synched_size = (readptr[3] << 0) | (readptr[2] << 7) | (readptr[1] << 14) | (readptr[0] << 21);
                    trace ("frame has extra size field = %d\n", synched_size);
                    readptr += 4;
                    sz -= 4;
                }
            }
            else if (version_major == 3) {
                if (flags1 & 0x1F) {
                    trace ("unknown status flags: %02x\n", flags1);
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x1F) {
                    trace ("unknown format flags: %02x\n", flags2);
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x80) {
                    trace ("frame is compressed, skipping\n");
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x40) {
                    trace ("frame is encrypted, skipping\n");
                    readptr += sz;
                    continue;
                }
                if (flags2 & 0x20) {
                    trace ("frame has group id\n");
                    readptr++; // skip id
                    sz--;
                }
            }

            // parse basic 2.3/2.4 text frames
            //const char *text_frames[] = { "TPE1", "TPE2", "TPOS", "TIT2", "TALB", "TCOP", "TCON", "TENC", "TPE3", "TCOM", "TRCK", "TYER", "TDRC", NULL };
            //char **text_holders[] = { &artist, &band, &disc, &title, &album, &copyright, &genre, &vendor, &performer, &composer, &track, version_major == 3 ? &year : NULL,  version_major == 4 ? &year : NULL, };
            if (it) {
                int added = 0;
                if (strcmp (frameid, "TXXX")) {
                    for (int f = 0; frame_mapping[f]; f += FRAME_MAPPINGS) {
                        const char *frm_name = version_major == 3 ? frame_mapping[f+MAP_ID3V23] : frame_mapping[f+MAP_ID3V24];
                        if (frm_name && !strcmp (frameid, frm_name)) {
                            added = 1;
                            if (synched_size > MAX_TEXT_FRAME_SIZE) {
                                trace ("frame %s is too big, discard\n", frameid);
                                break;
                            }

                            char *text = convstr_id3v2 (version_major, readptr[0], readptr+1, synched_size-1);

                            // couple of simple tests
                            //char *text = convstr_id3v2 (4, 3, "текст1\0текст2", strlen ("текст1")*2+2);
                            //const char ucstext[] = { 0x42, 0x04, 0x35, 0x04, 0x3a, 0x04, 0x41, 0x04, 0x42, 0x04, 0x31, 0x00, 0x00, 0x00, 0x42, 0x04, 0x35, 0x04, 0x3a, 0x04, 0x41, 0x04, 0x42, 0x04, 0x32, 0x00 };
                            //char *text = convstr_id3v2 (4, 1, ucstext, sizeof (ucstext));

                            if (text && *text) {
                                if (!strcmp (frameid, "TRCK")) { // special case for track/totaltracks
                                    junk_add_track_meta (it, text);
                                }
                                else if (!strcmp (frameid, "TCON")) {
                                    junk_id3v2_add_genre (it, text);
                                }
                                else {
                                    pl_append_meta (it, frame_mapping[f+MAP_DDB], text);
                                }
//                                if (text) {
//                                    trace ("%s = %s\n", frameid, text);
//                                }
                            }
                            if (text) {
                                free (text);
                            }
                            break;
                        }
                    }
                }

                if (added) {
                    readptr += sz;
                    continue;
                }

                if (!strcmp (frameid, "COMM")) {
                    if (sz < 4) {
                        trace ("COMM frame is too short, skipped\n");
                        readptr += sz; // bad tag
                        continue;
                    }

                    /*int res = */junk_load_comm_frame (version_major, it, readptr, synched_size);
                }
                else if (it && !strcmp (frameid, "RVA2")) {
                    if (synched_size < 5) {
                        trace ("RVA2 frame is too short, skipped\n");
                        readptr += sz; // bad tag
                        continue;
                    }

                    /*int res = */junk_id3v2_load_rva2(version_major, it, readptr, synched_size);
                }
                else if (it && !strcmp (frameid, "TXXX")) {
                    if (synched_size < 2) {
                        trace ("TXXX frame is too short, skipped\n");
                        readptr += sz; // bad tag
                        continue;
                    }
                    int res = junk_id3v2_load_txx (version_major, it, readptr, synched_size);
                }
            }
            readptr += sz;
        }
        else if (version_major == 2) {
            char frameid[4];
            memcpy (frameid, readptr, 3);
            frameid[3] = 0;
            readptr += 3;
            if (readptr - tag >= size - 3) {
                break;
            }
            uint32_t sz = (readptr[2] << 0) | (readptr[1] << 8) | (readptr[0] << 16);
            readptr += 3;
            if (readptr - tag >= size - sz) {
                break; // size of frame is less than size of tag
            }
            if (sz < 1) {
                break; // frame must be at least 1 byte long
            }
            if (sz > MAX_ID3V2_FRAME_SIZE) {
                trace ("junk_id3v2_read_full: frame %s size is too big, discarded\n", frameid);
                readptr += sz;
                continue;
            }
            int synched_size = sz;
            if (unsync) {
                synched_size = junklib_id3v2_sync_frame (readptr, sz);
            }

            if (tag_store) {
                DB_id3v2_frame_t *frm = malloc (sizeof (DB_id3v2_frame_t) + sz);
                if (!frm) {
                    fprintf (stderr, "junklib: failed to alloc %d bytes for id3v2.2 frame %s\n", (int)(sizeof (DB_id3v2_frame_t) + sz), frameid);
                    goto error;
                }
                memset (frm, 0, sizeof (DB_id3v2_frame_t));
                if (tail) {
                    tail->next = frm;
                }
                tail = frm;
                if (!tag_store->frames) {
                    tag_store->frames = frm;
                }
                strcpy (frm->id, frameid);
                memcpy (frm->data, readptr, synched_size);
                frm->size = sz;
            }
//            trace ("found id3v2.2 frame: %s, size=%d\n", frameid, sz);

            // parse basic 2.2 text frames
            if (it) {
                int added = 0;
                if (strcmp (frameid, "TXX")) {
                    for (int f = 0; frame_mapping[f]; f++) {
                        if (frame_mapping[f+MAP_ID3V22] && !strcmp (frameid, frame_mapping[f+MAP_ID3V22])) {
                            added = 1;
                            if (synched_size > MAX_TEXT_FRAME_SIZE) {
                                trace ("frame %s is too big, discard\n", frameid);
                                break;
                            }
                            char *text = convstr_id3v2 (version_major, readptr[0], readptr+1, synched_size-1);
                            if (text && *text) {
                                if (!strcmp (frameid, "TRCK")) { // special case for track/totaltracks
                                    junk_add_track_meta (it, text);
                                }
                                else if (!strcmp (frameid, "TCON")) {
                                    junk_id3v2_add_genre (it, text);
                                }
                                else {
                                    pl_append_meta (it, frame_mapping[f+MAP_DDB], text);
                                }
                                free (text);
                            }
                            break;
                        }
                    }
                }

                if (added) {
                    readptr += sz;
                    continue;
                }

                if (!strcmp (frameid, "COM")) {
                    if (synched_size < 6) {
                        readptr += sz;
                        continue;
                    }
                    /*int res = */junk_load_comm_frame (version_major, it, readptr, synched_size);
                }
                else if (it && !strcmp (frameid, "TXX")) {
                    if (synched_size < 2) {
                        trace ("TXX frame is too short, skipped\n");
                        readptr += sz; // bad tag
                        continue;
                    }
                    int res = junk_id3v2_load_txx (version_major, it, readptr, synched_size);
                }
            }
            readptr += sz;
        }
        else {
            trace ("id3v2.%d (unsupported!)\n", version_minor);
        }
    }
    if (it) {
        if (version_major == 2) {
            uint32_t f = pl_get_item_flags (it);
            f |= DDB_TAG_ID3V22;
            pl_set_item_flags (it, f);
        }
        else if (version_major == 3) {
            uint32_t f = pl_get_item_flags (it);
            f |= DDB_TAG_ID3V23;
            pl_set_item_flags (it, f);
        }
        else if (version_major == 4) {
            uint32_t f = pl_get_item_flags (it);
            f |= DDB_TAG_ID3V24;
            pl_set_item_flags (it, f);
        }
    }
    err = 0;
error:
    if (err != 0) {
        trace ("error parsing id3v2\n");
    }

    if (tag) {
        free (tag);
    }
    if (tag_store && err != 0) {
        while (tag_store->frames) {
            DB_id3v2_frame_t *next = tag_store->frames->next;
            free (tag_store->frames);
            tag_store->frames = next;
        }
    }
    return err;
}

int
junk_id3v2_read (playItem_t *it, DB_FILE *fp) {
    return junk_id3v2_read_full (it, NULL, fp);
}

const char *
junk_detect_charset (const char *s) {
    size_t len = strlen (s);

    // check if that's already utf8
    if (u8_valid (s, len, NULL)) {
        return NULL; // means no recoding required
    }
    // hack to add cp936 support
    if (can_be_chinese (s, len)) {
       return "cp936";
    }
    // check if that could be non-latin1 (too many nonascii chars)
    if (can_be_russian (s, len)) {
        return "cp1251";
    }

    return "cp1252";
}

int
junk_recode (const char *in, int inlen, char *out, int outlen, const char *cs) {
    return junk_iconv (in, inlen, out, outlen, cs, UTF8_STR);
}

int
junk_rewrite_tags (playItem_t *it, uint32_t junk_flags, int id3v2_version, const char *id3v1_encoding) {
    trace ("junk_rewrite_tags %X\n", junk_flags);
    int err = -1;
    char *buffer = NULL;
    DB_FILE *fp = NULL;
    FILE *out = NULL;

    uint32_t item_flags = pl_get_item_flags (it);

    // get options
    int strip_id3v2 = junk_flags & JUNK_STRIP_ID3V2;
    int strip_id3v1 = junk_flags & JUNK_STRIP_ID3V1;
    int strip_apev2 = junk_flags & JUNK_STRIP_APEV2;
    int write_id3v2 = junk_flags & JUNK_WRITE_ID3V2;
    int write_id3v1 = junk_flags & JUNK_WRITE_ID3V1;
    int write_apev2 = junk_flags & JUNK_WRITE_APEV2;

    char tmppath[PATH_MAX];
    // find the beginning and the end of audio data
    char fname[PATH_MAX];
    pl_get_meta (it, ":URI", fname, sizeof (fname));
    snprintf (tmppath, sizeof (tmppath), "%s.temp", fname);
    fp = deadbeef->fopen (fname);
    if (!fp) {
        trace ("file not found %s\n", fname);
        return -1;
    }

    int fsize = deadbeef->fgetlength (fp);
    int id3v2_size = 0;
    int id3v2_start = deadbeef->junk_id3v2_find (fp, &id3v2_size);
    if (id3v2_start == -1) {
        id3v2_size = -1;
    }

    int32_t apev2_size;
    uint32_t flags, numitems;
    int apev2_start = deadbeef->junk_apev2_find (fp, &apev2_size, &flags, &numitems);
    if (apev2_start == -1) {
        apev2_start = 0;
    }

    if (!strip_apev2 && !write_apev2) {
        apev2_start = 0;
    }

    int id3v1_start = deadbeef->junk_id3v1_find (fp);
    if (id3v1_start == -1) {
        id3v1_start = 0;
    }

    int header = 0;
    if (id3v2_size > 0) {
        header = id3v2_start + id3v2_size;
    }

    int footer = fsize;

    if (id3v1_start > 0) {
        footer = id3v1_start;
    }
    if (apev2_start > 0) {
        footer = min (footer, apev2_start);
    }

    trace ("header size: %d, footer size: %d\n", header, fsize-footer);

    // "TRCK" -- special case
    // "TYER"/"TDRC" -- special case

    // open output file
    out = NULL;

    out = fopen (tmppath, "w+b");
    trace ("will write tags into %s\n", tmppath);
    if (!out) {
        fprintf (stderr, "cmp3_write_metadata: failed to open temp file %s\n", tmppath);
        goto error;
    }

    DB_id3v2_tag_t id3v2;
    DB_apev2_tag_t apev2;

    memset (&id3v2, 0, sizeof (id3v2));
    memset (&apev2, 0, sizeof (apev2));

    if (!strip_id3v2 && !write_id3v2 && id3v2_size > 0) {
        if (deadbeef->fseek (fp, id3v2_start, SEEK_SET) == -1) {
            trace ("cmp3_write_metadata: failed to seek to original id3v2 tag position in %s\n", pl_find_meta (it, ":URI"));
            goto error;
        }
        uint8_t *buf = malloc (id3v2_size);
        if (!buf) {
            trace ("cmp3_write_metadata: failed to alloc %d bytes for id3v2 tag\n", id3v2_size);
            goto error;
        }
        if (deadbeef->fread (buf, 1, id3v2_size, fp) != id3v2_size) {
            trace ("cmp3_write_metadata: failed to read original id3v2 tag from %s\n", pl_find_meta (it, ":URI"));
            free (buf);
            goto error;
        }
        if (fwrite (buf, 1, id3v2_size, out) != id3v2_size) {
            trace ("cmp3_write_metadata: failed to copy original id3v2 tag from %s to temp file\n", pl_find_meta (it, ":URI"));
            free (buf);
            goto error;
        }
        free (buf);
    }
    else if (write_id3v2) {
        trace ("writing id3v2\n");
        if (id3v2_size <= 0 || strip_id3v2 || deadbeef->junk_id3v2_read_full (NULL, &id3v2, fp) != 0) {
            deadbeef->junk_id3v2_free (&id3v2);
            memset (&id3v2, 0, sizeof (id3v2));
            id3v2.version[0] = id3v2_version;
        }
        // convert to required version
        while (id3v2.version[0] != id3v2_version) {
            DB_id3v2_tag_t converted;
            memset (&converted, 0, sizeof (converted));
            if (id3v2.version[0] == 2) {
                if (deadbeef->junk_id3v2_convert_22_to_24 (&id3v2, &converted) != 0) {
                    goto error;
                }
                deadbeef->junk_id3v2_free (&id3v2);
                memcpy (&id3v2, &converted, sizeof (DB_id3v2_tag_t));
                continue;
            }
            else if (id3v2.version[0] == 3) {
                if (deadbeef->junk_id3v2_convert_23_to_24 (&id3v2, &converted) != 0) {
                    goto error;
                }
                deadbeef->junk_id3v2_free (&id3v2);
                memcpy (&id3v2, &converted, sizeof (DB_id3v2_tag_t));
                continue;
            }
            else if (id3v2.version[0] == 4) {
                if (deadbeef->junk_id3v2_convert_24_to_23 (&id3v2, &converted) != 0) {
                    goto error;
                }
                deadbeef->junk_id3v2_free (&id3v2);
                memcpy (&id3v2, &converted, sizeof (DB_id3v2_tag_t));
                continue;
            }
        }

        junk_id3v2_remove_all_txxx_frames (&id3v2);

        // COMM
        junk_id3v2_remove_frames (&id3v2, "COMM");
        pl_lock ();
        {
            const char *val = pl_find_meta (it, "comment");
            if (val && *val) {
                junk_id3v2_remove_frames (&id3v2, "COMM");
                junk_id3v2_add_comment_frame (&id3v2, "eng", "", val);
            }
        }
        pl_unlock ();

        // remove all known normal frames (they will be refilled from track metadata)
        int idx = id3v2.version[0] == 3 ? MAP_ID3V23 : MAP_ID3V24;
        for (int i = 0; frame_mapping[i]; i += FRAME_MAPPINGS) {
            if (frame_mapping[i+idx]) {
                junk_id3v2_remove_frames (&id3v2, frame_mapping[i+idx]);
                trace ("removed frame %s\n", frame_mapping[i+idx]);
            }
        }

        DB_metaInfo_t *meta = pl_get_metadata_head (it);
        while (meta) {
            if (meta->value && *meta->value) {
                int i;
                for (i = 0; frame_mapping[i]; i += FRAME_MAPPINGS) {
                    if (!strcasecmp (meta->key, frame_mapping[i+MAP_DDB])) {
                        const char *frm_name = id3v2_version == 3 ? frame_mapping[i+MAP_ID3V23] : frame_mapping[i+MAP_ID3V24];
                        if (frm_name) {
                            // field is known and supported for this tag version
                            trace ("add_frame %s %s\n", frm_name, meta->value);
                            junk_id3v2_add_text_frame (&id3v2, frm_name, meta->value);
                        }
                        break;
                    }
                }
                if (!frame_mapping[i]
                        && meta->key[0] != ':'
                        && strcasecmp (meta->key, "comment")
                        && strcasecmp (meta->key, "track")
                        && strcasecmp (meta->key, "numtracks")) {
                    // add as txxx
                    trace ("adding unknown frame as TXX %s=%s\n", meta->key, meta->value);
                    junk_id3v2_remove_txxx_frame (&id3v2, meta->key);
                    junk_id3v2_add_txxx_frame (&id3v2, meta->key, meta->value);
                }
            }
            meta = meta->next;
        }

        // add tracknumber/totaltracks
        pl_lock ();
        {
            const char *track = pl_find_meta (it, "track");
            const char *totaltracks = pl_find_meta (it, "numtracks");
            if (track && totaltracks) {
                char s[100];
                snprintf (s, sizeof (s), "%s/%s", track, totaltracks);
                junk_id3v2_remove_frames (&id3v2, "TRCK");
                junk_id3v2_add_text_frame (&id3v2, "TRCK", s);
            }
            else if (track) {
                junk_id3v2_remove_frames (&id3v2, "TRCK");
                junk_id3v2_add_text_frame (&id3v2, "TRCK", track);
            }
        }
        pl_unlock ();

        // write tag
        if (junk_id3v2_write (out, &id3v2) != 0) {
            trace ("cmp3_write_metadata: failed to write id3v2 tag to %s\n", pl_find_meta (it, ":URI"))
            goto error;
        }
    }

    // now write audio data
    buffer = malloc (8192);
    deadbeef->fseek (fp, header, SEEK_SET);
    int writesize = fsize;
    if (footer > 0) {
        writesize -= (fsize - footer);
    }
    writesize -= header;
    trace ("writesize: %d, id3v1_start: %d(%d), apev2_start: %d, footer: %d\n", writesize, id3v1_start, fsize-id3v1_start, apev2_start, footer);

    while (writesize > 0) {
        int rb = min (8192, writesize);
        rb = deadbeef->fread (buffer, 1, rb, fp);
        if (rb < 0) {
            fprintf (stderr, "junk_write_id3v2: error reading input data\n");
            goto error;
        }
        if (fwrite (buffer, 1, rb, out) != rb) {
            fprintf (stderr, "junk_write_id3v2: error writing output file\n");
            goto error;
        }
        if (rb == 0) {
            break; // eof
        }
        writesize -= rb;
    }

    if (!write_apev2 && !strip_apev2 && apev2_start != 0) {
        trace ("copying original apev2 tag\n");
        if (deadbeef->fseek (fp, apev2_start, SEEK_SET) == -1) {
            trace ("cmp3_write_metadata: failed to seek to original apev2 tag position in %s\n", pl_find_meta (it, ":URI"));
            goto error;
        }
        uint8_t *buf = malloc (apev2_size);
        if (!buf) {
            trace ("cmp3_write_metadata: failed to alloc %d bytes for apev2 tag\n", apev2_size);
            goto error;
        }
        if (deadbeef->fread (buf, 1, apev2_size, fp) != apev2_size) {
            trace ("cmp3_write_metadata: failed to read original apev2 tag from %s\n", pl_find_meta (it, ":URI"));
            free (buf);
            goto error;
        }
        if (fwrite (buf, 1, apev2_size, out) != apev2_size) {
            trace ("cmp3_write_metadata: failed to copy original apev2 tag from %s to temp file\n", pl_find_meta (it, ":URI"));
            free (buf);
            goto error;
        }
        free (buf);
    }
    else if (write_apev2) {
        trace ("writing new apev2 tag (strip=%d)\n", strip_apev2);
        if (strip_apev2 || junk_apev2_read_full (NULL, &apev2, fp) != 0) {
            deadbeef->junk_apev2_free (&apev2);
            memset (&apev2, 0, sizeof (apev2));
        }

        // remove all text frames
        junk_apev2_remove_all_text_frames (&apev2);

        // add all basic frames
        DB_metaInfo_t *meta = pl_get_metadata_head (it);
        while (meta) {
            if (meta->value && *meta->value) {
                int i;
                for (i = 0; frame_mapping[i]; i += FRAME_MAPPINGS) {
                    if (!strcasecmp (meta->key, frame_mapping[i+MAP_DDB]) && frame_mapping[i+MAP_APEV2]) {
                        trace ("apev2 writing known field: %s=%s\n", meta->key, meta->value);
                        junk_apev2_add_text_frame (&apev2, frame_mapping[i+MAP_APEV2], meta->value);
                        break;
                    }
                }
                if (!frame_mapping[i]
                        && meta->key[0] != ':'
                        && strcasecmp (meta->key, "track")
                        && strcasecmp (meta->key, "numtracks")) {
                    trace ("apev2 writing unknown field: %s=%s\n", meta->key, meta->value);
                    junk_apev2_add_text_frame (&apev2, meta->key, meta->value);
                }
            }
            meta = meta->next;
        }

        // add tracknumber/totaltracks
        pl_lock ();
        {
            const char *track = pl_find_meta (it, "track");
            const char *totaltracks = pl_find_meta (it, "numtracks");
            if (track && totaltracks) {
                char s[100];
                snprintf (s, sizeof (s), "%s/%s", track, totaltracks);
                junk_apev2_remove_frames (&apev2, "Track");
                junk_apev2_add_text_frame (&apev2, "Track", s);
            }
            else if (track) {
                junk_apev2_remove_frames (&apev2, "Track");
                junk_apev2_add_text_frame (&apev2, "Track", track);
            }
        }
        pl_unlock ();

        // write tag
        if (deadbeef->junk_apev2_write (out, &apev2, 0, 1) != 0) {
            trace ("cmp3_write_metadata: failed to write apev2 tag to %s\n", pl_find_meta (it, ":URI"))
            goto error;
        }
    }

    if (!write_id3v1 && !strip_id3v1 && id3v1_start != 0) {
        trace ("copying original id3v1 tag %d %d %d\n", write_id3v1, strip_id3v1, id3v1_start);
        if (deadbeef->fseek (fp, id3v1_start, SEEK_SET) == -1) {
            trace ("cmp3_write_metadata: failed to seek to original id3v1 tag position in %s\n", pl_find_meta (it, ":URI"));
            goto error;
        }
        char buf[128];
        if (deadbeef->fread (buf, 1, 128, fp) != 128) {
            trace ("cmp3_write_metadata: failed to read original id3v1 tag from %s\n", pl_find_meta (it, ":URI"));
            goto error;
        }
        if (fwrite (buf, 1, 128, out) != 128) {
            trace ("cmp3_write_metadata: failed to copy id3v1 tag from %s to temp file\n", pl_find_meta (it, ":URI"));
            goto error;
        }
    }
    else if (write_id3v1) {
        trace ("writing new id3v1 tag\n");
        if (junk_id3v1_write (out, it, id3v1_encoding) != 0) {
            trace ("cmp3_write_metadata: failed to write id3v1 tag to %s\n", pl_find_meta (it, ":URI"))
            goto error;
        }
    }

    if (strip_id3v1 && !write_id3v1) {
        item_flags &= ~DDB_TAG_ID3V1;
    }
    if (strip_id3v2 && !write_id3v2) {
        item_flags &= ~(DDB_TAG_ID3V22|DDB_TAG_ID3V23|DDB_TAG_ID3V24);
    }
    if (strip_apev2 && !write_apev2) {
        item_flags &= ~DDB_TAG_APEV2;
    }

    if (write_id3v1) {
        item_flags |= DDB_TAG_ID3V1;
    }
    if (write_id3v2) {
        item_flags &= ~(DDB_TAG_ID3V22|DDB_TAG_ID3V23|DDB_TAG_ID3V24);
        item_flags |= id3v2_version == 3 ? DDB_TAG_ID3V23 : DDB_TAG_ID3V24;
    }
    if (write_apev2) {
        item_flags |= DDB_TAG_APEV2;
    }

    pl_set_item_flags (it, item_flags);
    err = 0;
error:
    if (fp) {
        deadbeef->fclose (fp);
    }
    if (out) {
        fclose (out);
    }
    if (buffer) {
        free (buffer);
    }
    if (!err) {
        pl_lock ();
        rename (tmppath, fname);
        pl_unlock ();
    }
    else {
        unlink (tmppath);
    }
    return err;
}

void
junk_enable_cp1251_detection (int enable) {
    enable_cp1251_detection = enable;
}

void
junk_enable_cp936_detection (int enable) {
    enable_cp936_detection = enable;
}

void
junk_configchanged (void) {
    int cp1251 = conf_get_int ("junk.enable_cp1251_detection", 1);
    int cp936 = conf_get_int ("junk.enable_cp936_detection", 0);
    junk_enable_cp1251_detection (cp1251);
    junk_enable_cp936_detection (cp936);
}
