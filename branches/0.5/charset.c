// charset.c (part of mintty)
// Copyright 2008-09  Andy Koppe
// Based on code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "charset.h"

#include "config.h"
#include "platform.h"

#include <locale.h>
#include <winbase.h>
#include <winnls.h>

// Constant for representing an unspecified charset.
#define CS_DEFAULT -1

static cs_mode mode = CSM_DEFAULT;
static uint env_codepage, default_codepage, codepage;

#if HAS_LOCALES
static const char *env_locale, *default_locale;
static bool use_locale;
#endif

static char system_locale[] = "xx_XX";

bool cs_ambig_wide;
int cs_cur_max;

extern bool font_ambig_wide;

static const struct {
  ushort id;
  const char *name;
}
cs_names[] = {
  {CP_UTF8, "UTF-8"},
  {  20127, "ASCII"},
  {  20866, "KOI8-R"},
  {  21866, "KOI8-U"},
  {    936, "GBK"},
  {    950, "Big5"},
  {    932, "SJIS"},
#if HAS_LOCALES
  {  51932, "eucJP"},  // CP20932 is a simplified DBCS version of the proper one
#endif
  {    949, "eucKR"},
  // Aliases
  {CP_UTF8, "UTF8"},
  {  20866, "KOI8"},
  // Not supported by Cygwin
  {  54396, "GB18030"},
  { CP_ACP, "ANSI"},
  { CP_OEMCP, "OEM"},
};

static const struct {
  ushort id;
  const char *comment;
}
cs_menu[] = {
  { CP_UTF8, "Unicode"},
  {   28591, "Western European"},
  {   28592, "Central European"},
  {   28593, "South European"},
  {   28594, "North European"},
  {   28595, "Cyrillic"},
  {   28596, "Arabic"},
  {   28597, "Greek"},
  {   28598, "Hebrew"},
  {   28599, "Turkish"},
#if HAS_LOCALES
  {   28600, "Nordic"},
  {   28601, "Thai"},
#endif
  {   28603, "Baltic"},
#if HAS_LOCALES
  {   28604, "Celtic"},
#endif
  {   28605, "\"euro\""},
#if HAS_LOCALES
  {   28606, "Balkans"},
#endif
  {   20866, "Russian"},
  {   21866, "Ukrainian"},
  {     936, "Chinese"},
  {     950, "Chinese"},
  {     932, "Japanese"},
#if HAS_LOCALES
  {   51932, "Japanese"},
#endif
  {     949, "Korean"},
};

static const char *const
locale_menu[] = {
  "ar", // Arabic
  "bn", // Bengali
  "de", // German
  "en", // English
  "es", // Spanish
  "fa", // Persian
  "fr", // French
  "hi", // Hindi
  "id", // Indonesian
  "it", // Italian
  "ja", // Japanese
  "ko", // Korean
  "pt", // Portuguese
  "ru", // Russian
  "th", // Thai
  "tr", // Turkish
  "ur", // Urdu
  "vi", // Vietnamese
  "zh", // Chinese
  "C",  // language-neutral
};

static void
strtoupper(char *dst, const char *src)
{
  while ((*dst++ = toupper((uchar)*src++)));
}

static const char *
cs_name(int id)
{
  if (id == CS_DEFAULT)
    return "";

  for (uint i = 0; i < lengthof(cs_names); i++) {
    if (id == cs_names[i].id)
      return cs_names[i].name;
  }
  
  static char buf[16];
  if (id >= 28591 && id <= 28606)
    sprintf(buf, "ISO-8859-%u", id - 28590);
  else
    sprintf(buf, "CP%u", id);
  return buf;
}

static int
cs_id(const char *name)
{
  if (*name) {
    uint id;
    char upname[strlen(name) + 1];
    strtoupper(upname, name);

    if (sscanf(upname, "ISO-8859-%u", &id) == 1) {
      if (id != 0 && id != 12 && id <= 16)
        return id + 28590;
    }
    else if (sscanf(upname, "CP%u", &id) == 1 ||
             sscanf(upname, "WIN%u", &id) == 1 ||
             sscanf(upname, "%u", &id) == 1) {
      CPINFO cpi;
      if (GetCPInfo(id, &cpi))
        return id;
    }
    else {
      for (uint i = 0; i < lengthof(cs_names); i++) {
        char cs_upname[8];
        strtoupper(cs_upname, cs_names[i].name);
        if (memcmp(upname, cs_upname, strlen(cs_upname)) == 0)
          return cs_names[i].id;
      }
    }
  }
  return CS_DEFAULT;
}

static uint
cs_codepage(const char *loc, char *cs)
{
  int id = cs_id(cs);
  if (id != CS_DEFAULT)
    return id;
  else if (HAS_UTF8_C_LOCALE && loc[0] == 'C' && (!loc[1] || loc[1] == '.'))
    return CP_UTF8;
  else 
    return CP_ACP;  
}  

void
correct_charset(char *cs)
{
  strcpy(cs, cs_name(cs_id(cs)));
}

void
correct_locale(char *locale)
{
  if (!strcmp(locale, "C"))
    return;
  uchar *lang = (uchar *)locale;
  if (isalpha(lang[0]) && isalpha(lang[1])) {
    // Treat two letters at the start as the language.
    locale[0] = tolower(lang[0]);
    locale[1] = tolower(lang[1]);
    uchar *terr = (uchar *)strchr(locale + 2, '_');
    if (terr && isalpha(terr[1]) && isalpha(terr[2])) {
      // Treat two letters after an underscore as the territory.
      locale[2] = '_';
      locale[3] = toupper(terr[1]);
      locale[4] = toupper(terr[2]);
      locale[5] = 0;
    }
    else
      locale[2] = 0;
  }
  else 
    locale[0] = 0;
}

const char *
enumerate_locales(uint i)
{
  if (i == 0)
    return "(None)";
  if (i == 1)
    return system_locale;
  i -= 2;
  if (i < lengthof(locale_menu))
    return locale_menu[i];
  return 0;
}

const char *
enumerate_charsets(uint i)
{
  if (i == 0)
    return "(Default)";
  static char buf[64];
  if (--i < lengthof(cs_menu)) {
    sprintf(buf, "%s (%s)", cs_name(cs_menu[i].id), cs_menu[i].comment);
    return buf;
  }
  if ((i -= lengthof(cs_menu)) < 2) {
    const char *cs = cs_name(i ? GetACP() : GetOEMCP());
    if (*cs == 'C') {
      sprintf(buf, "%s (%s codepage)", cs, i ? "ANSI" : "OEM");
      return buf;
    }
  }
  return 0;
}

const char *
cs_init(void)
{
  GetLocaleInfo(
    LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, system_locale, 2
  );
  GetLocaleInfo(
    LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, system_locale + 3, 2
  );

  char *locale =
    getenv("LC_ALL") ?: getenv("LC_CTYPE") ?: getenv("LANG") ?: "C";
#if HAS_LOCALES
  env_locale = strdup(locale);
#endif
  char *dot = strchr(locale, '.');
  char *charset = dot ? dot + 1 : "";
  env_codepage = cs_codepage(locale, charset);

  return cs_config();
}

static int
cp_cur_max(void)
{
  CPINFO cpinfo;
  GetCPInfo(codepage, &cpinfo);
  return cpinfo.MaxCharSize;
}

static void
cs_update(void)
{
  codepage = 
    mode == CSM_UTF8 ? CP_UTF8 : mode == CSM_OEM  ? 437 : default_codepage;

#if HAS_LOCALES
  use_locale = (mode == CSM_DEFAULT && default_locale) || mode == CSM_UTF8;
  if (use_locale) {
    setlocale(LC_CTYPE,
      mode == CSM_DEFAULT
      ? default_locale
      : cs_ambig_wide ? "ja.UTF-8" : "en.UTF-8"
    );
    cs_cur_max = MB_CUR_MAX;
  }
  else
    cs_cur_max = cp_cur_max();
#else
  cs_cur_max = cp_cur_max();
#endif

  // Clear output conversion state.
  cs_mb1towc(0, 0);
}

const char *
cs_config(void)
{
  static char locale[32];
  bool override_env = *cfg.locale;

  if (override_env) {
    default_codepage = cs_codepage(cfg.locale, cfg.charset);
    if (*cfg.charset)
      sprintf(locale, "%s.%s", cfg.locale, cfg.charset);
    else
      strcpy(locale, cfg.locale);
  }
  else
    default_codepage = env_codepage;

#if HAS_LOCALES
  default_locale = override_env ? locale : env_locale;
  
  if (!setlocale(LC_CTYPE, default_locale)) {
    // Not a valid Cygwin locale: fall back to Windows functions.
    default_locale = 0;
    cs_ambig_wide = font_ambig_wide;
  }
  else {
    cs_ambig_wide = wcwidth(0x3B1) == 2;
    if (override_env && cs_ambig_wide && !font_ambig_wide) {
      // Attach "@cjknarrow" to locale if using an ambig-narrow font
      // with an ambig-wide locale setting
      strcat(locale, "@cjknarrow");
      cs_ambig_wide = false;
    }
  }
#else
  cs_ambig_wide = font_ambig_wide;
#endif

  cs_update();
  return override_env ? locale : 0;
}

void
cs_set_mode(cs_mode new_mode)
{
  if (new_mode != mode) {
    mode = new_mode;
    cs_update();
  }
}

int
cs_wcntombn(char *s, const wchar *ws, size_t len, size_t wlen)
{
#if HAS_LOCALES
  if (use_locale) {
    // The POSIX way
    size_t i = 0, wi = 0;
    len -= MB_CUR_MAX;
    while (wi < wlen && i <= len) {
      int n = wctomb(&s[i], ws[wi++]);
      // Drop untranslatable characters.
      if (n >= 0)
        i += n;
    }
    return i;
  }
#endif
  return WideCharToMultiByte(codepage, 0, ws, wlen, s, len, 0, 0);
}

int
cs_mbstowcs(wchar *ws, const char *s, size_t wlen)
{
#if HAS_LOCALES
  if (use_locale)
    return mbstowcs(ws, s, wlen);
#endif
  return MultiByteToWideChar(codepage, 0, s, -1, ws, wlen) - 1;
}

int
cs_mb1towc(wchar *pwc, const char *pc)
{
#if HAS_LOCALES
  if (use_locale)
    return mbrtowc(pwc, pc, 1, 0);
#endif

  // The Windows way
  static int sn;
  static char s[8];
  static wchar ws[2];

  if (!pc) {
    // Reset state
    sn = 0;
    return 0;
  }
  if (sn < 0) {
    // Leftover surrogate
    *pwc = ws[1];
    sn = 0;
    return 0;
  }
  if (sn == cs_cur_max)
    return -1; // Overlong sequence
  s[sn++] = *pc;
  switch (MultiByteToWideChar(codepage, 0, s, sn, ws, 2)) {
    when 1:
      if (*ws == 0xFFFD)
        return -2; // Incomplete character
      else
        sn = 0; // Valid character
    when 2:
      if (*ws == 0xFFFD)
        return -1; // Encoding error
      else
        sn = -1; // Surrogate pair
    when 0:
      return -2; // pre-Vista: can't tell errors from incomplete chars :(
  }
  *pwc = *ws;
  return 1;
}

wchar
cs_btowc_glyph(char c)
{
  wchar wc = 0;
  MultiByteToWideChar(codepage, MB_USEGLYPHCHARS, &c, 1, &wc, 1);
  return wc;
}
