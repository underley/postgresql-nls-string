#include <string.h>
#include <stdlib.h>
#include <locale.h>

#include "postgres.h"
#include "fmgr.h"

PG_MODULE_MAGIC;

static char *lc_collate_cache = NULL;
static int multiplication = 1;

extern Datum nls_string(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(nls_string);
extern Datum nls_value(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(nls_value);

#define _NLS_AS_STRING 0
#define _NLS_AS_BYTEA 1

text * _nls_run_strxfrm(text *string, text *locale, int result_type);
text * _nls_run_strxfrm(text *string, text *locale, int result_type)
{
	char *string_str;
	int string_len;

	char *locale_str = NULL;
	int locale_len = 0;

	text *result;
	char *tmp = NULL;
	size_t size = 0;
	size_t rest = 0;
	int i;
	int changed_locale = 0;

	/*
	 * Save the default, server-wide locale setting.
	 * It should not change during the life-span of the server so it
	 * is safe to save it only once, during the first invocation.
	 */
	if (!lc_collate_cache)
	{
		if ((lc_collate_cache = setlocale(LC_COLLATE, NULL)))
			/* Make a copy of the locale name string. */
			lc_collate_cache = strdup(lc_collate_cache);
		if (!lc_collate_cache)
			elog(ERROR, "failed to retrieve the default LC_COLLATE value");
	}

	/*
	 * To run strxfrm, we need a zero-terminated strings.
	 */
	string_len = VARSIZE(string) - VARHDRSZ;
	if (string_len < 0)
		return NULL;
	string_str = palloc(string_len + 1);
	memcpy(string_str, VARDATA(string), string_len);
	*(string_str + string_len) = '\0';

	if (locale)
	{
		locale_len = VARSIZE(locale) - VARHDRSZ;
	}
	/*
	 * If different than default locale is requested, call setlocale.
	 */
	if (locale_len > 0
		&& (strncmp(lc_collate_cache, VARDATA(locale), locale_len)
			|| *(lc_collate_cache + locale_len) != '\0'))
	{
		locale_str = palloc(locale_len + 1);
		memcpy(locale_str, VARDATA(locale), locale_len);
		*(locale_str + locale_len) = '\0';

		/*
		 * Try to set correct locales.
		 * If setlocale failed, we know the default stayed the same,
		 * co we can safely elog.
		 */
		if (!setlocale(LC_COLLATE, locale_str))
			elog(ERROR, "failed to set the requested LC_COLLATE value [%s]", locale_str);
		
		changed_locale = 1;
	}

	/*
	 * We do TRY / CATCH / END_TRY to catch ereport / elog that might
	 * happen during palloc. Ereport during palloc would not be 
	 * nice since it would leave the server with changed locales 
	 * setting, resulting in bad things.
	 */
	PG_TRY();
	{

		/*
		 * Text transformation.
		 * Increase the buffer until the strxfrm is able to fit.
		 */
		size = string_len * multiplication + 1;
		tmp = palloc(size + ((result_type == _NLS_AS_BYTEA) * VARHDRSZ));

		rest = strxfrm(tmp + ((result_type == _NLS_AS_BYTEA) * VARHDRSZ), string_str, size);
		while (rest >= size) 
		{
			pfree(tmp);
			size = rest + 1;
			tmp = palloc(size + ((result_type == _NLS_AS_BYTEA) * VARHDRSZ));
			rest = strxfrm(tmp + ((result_type == _NLS_AS_BYTEA) * VARHDRSZ), string_str, size);
			/*
			 * Cache the multiplication factor so that the next 
			 * time we start with better value.
			 */
			if (string_len)
				multiplication = (rest / string_len) + 2;
		}
	}
	PG_CATCH ();
	{
		if (changed_locale) {
			/*
			 * Set original locale
			 */
			if (!setlocale(LC_COLLATE, lc_collate_cache))
				elog(FATAL, "failed to set back the default LC_COLLATE value [%s]", lc_collate_cache);
		}
	}
	PG_END_TRY ();

	if (changed_locale)
	{
		/*
		 * Set original locale
		 */
		if (!setlocale(LC_COLLATE, lc_collate_cache))
			elog(FATAL, "failed to set back the default LC_COLLATE value [%s]", lc_collate_cache);
		pfree(locale_str);
	}
	pfree(string_str);

	/*
	 * If the multiplication factor went down, reset it.
	 */
	if (string_len && rest < string_len * multiplication / 4)
		multiplication = (rest / string_len) + 1;

	if (result_type == _NLS_AS_BYTEA)
	{
		result = (text *) tmp;
		SET_VARSIZE(result, rest + VARHDRSZ);
		return result;
	}
	else
	{
		/*
		 * Transformation to unsigned octal.
		 * We need one byte extra for sprintf's zero at the end.
		 */
		result = (text *) palloc(3 * rest + VARHDRSZ + 1);
	
		for (i = 0; i < rest; i++) 
		{
			sprintf(VARDATA(result) + 3 * i, "%03o",
			(int)(unsigned char)*(tmp + i));
		}
		pfree(tmp);
		SET_VARSIZE(result, 3 * rest + VARHDRSZ);
		return result;
	}
}

Datum
nls_string(PG_FUNCTION_ARGS) 
{
	text *locale;
	text *result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();
	if (PG_ARGISNULL(1))
	{
		locale = palloc(VARHDRSZ);
		SET_VARSIZE(locale, 0);
	}
	else
	{
		locale = PG_GETARG_TEXT_P(1);
	}

	result = _nls_run_strxfrm(PG_GETARG_TEXT_P(0), locale, _NLS_AS_STRING);

	if (! result)
		PG_RETURN_NULL();

	PG_RETURN_TEXT_P(result);
}

Datum
nls_value(PG_FUNCTION_ARGS) 
{
	text *locale;
	text *result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();
	if (PG_ARGISNULL(1))
	{
		locale = palloc(VARHDRSZ);
		SET_VARSIZE(locale, 0);
	}
	else
	{
		locale = PG_GETARG_TEXT_P(1);
	}

	result = _nls_run_strxfrm(PG_GETARG_TEXT_P(0), locale, _NLS_AS_BYTEA);

	if (! result)
		PG_RETURN_NULL();

	PG_RETURN_BYTEA_P(result);
}

