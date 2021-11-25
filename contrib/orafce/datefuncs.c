/*-------------------------------------------------------------------------
 *
 *
 * Copyright (c) 2001-2021, highgo compatible Group
 *
 *
 * IDENTIFICATION
 *        contrib/orafce /
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "access/xact.h"
#include "commands/variable.h"
#include "mb/pg_wchar.h"
#include "utils/date.h"
#include "utils/builtins.h"
#include "utils/numeric.h"
#include "utils/formatting.h"
#include <sys/time.h>
#include "builtins.h"
#include "pgtime.h"
#include "utils/datetime.h"
#include "utils/numeric.h"
#include "port.h"
#include "miscadmin.h"
#include <utils/date.h>
#include <funcapi.h>


PG_FUNCTION_INFO_V1(ora_fromtz);
PG_FUNCTION_INFO_V1(ora_systimestamp);
PG_FUNCTION_INFO_V1(ora_sys_extract_utc);
PG_FUNCTION_INFO_V1(ora_days_between);
PG_FUNCTION_INFO_V1(ora_days_between_tmtz);



static bool
valid_zone(char *tzname)
{
	pg_tz	   *tz;
	pg_tzenum  *tzenum;
	int tzoff;
	struct pg_tm tm;
	fsec_t fsec;
	const char *tzn;
	tzenum = pg_tzenumerate_start();
	for (;;)
	{
		tz = pg_tzenumerate_next(tzenum);
		if (!tz)
		{
			pg_tzenumerate_end(tzenum);
			return -1;
		}
		if (timestamp2tm(GetCurrentTransactionStartTimestamp(),
						 &tzoff, &tm, &fsec, &tzn, tz) != 0)
			continue;
		if (tzn && strlen(tzn) > 31)
			continue;
		if (strncmp(pg_get_timezone_name(tz),tzname,strlen(tzname)) == 0 ||
			strncmp(tzn,tzname,strlen(tzname)) == 0)
		{
			pg_tzenumerate_end(tzenum);
			return 0;
		}
	}
	return -1;
}

/********************************************************************
 *
 * ora_fromtz
 *
 * Purpose:
 *
 * Returns the specified time zone as the time
 *
 ********************************************************************/
Datum
ora_fromtz(PG_FUNCTION_ARGS)
{
		Timestamp timestamp = PG_GETARG_TIMESTAMP(0);
		text *zone = PG_GETARG_TEXT_PP(1);
		char tzname[TZ_STRLEN_MAX+1];
		TimestampTz result;
		struct pg_tm tt,
		*tm = &tt;
		fsec_t  fsec;
		int tz,tz_1;
		char dtstr[TZ_STRLEN_MAX+1];

		int dtlen = 0;
		int tzlen = 0;

		char *resval;
		/*convert timestamp to tm structure*/
		if (timestamp2tm(timestamp, NULL, tm, &fsec, NULL, NULL) != 0)
				ereport(ERROR,
				(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
				errmsg("timestamp out of range")));

		/*convert  tm structure to timestamp*/
		if (tm2timestamp(tm, fsec, &tz, &result) != 0)
				ereport(ERROR,
				(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
				errmsg("timestamp out of range")));

		/*encode the datetime*/
		EncodeDateTime(tm, fsec, false, tz, NULL, DateStyle, dtstr);

		dtlen = strlen(dtstr);

		/*call pg functions to switch text to cstring*/
		text_to_cstring_buffer(zone, tzname, sizeof(tzname));
		tzlen = strlen(tzname)+1;
	if (isdigit((unsigned char)*tzname))
	{
		for(int i=tzlen ;i >= 0 ;i--)
		{
			tzname[i+1]=tzname[i];
		}
		tzname[0]='+';
		tz_1=DecodeTimezone(tzname,&tz);
		if (tz_1)
					ereport(ERROR,
					(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
					errmsg("timeszone must be between -16 and 16 ")));
	}
	else if (*tzname == '+' || *tzname == '-')
	{
		tz_1=DecodeTimezone(tzname,&tz);
		if (tz_1)
					ereport(ERROR,
					(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
					errmsg("timeszone must be between -16 and 16 ")));
	}
	else if (isalpha((unsigned char)*tzname))
	{
		if(valid_zone(tzname))
		{
			ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("times zone unrecognized")));
		}
	}
	else
	{
			ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("times zone unrecognized")));
	}
		resval = (char *)palloc(dtlen + tzlen + 2);

		sprintf(resval, "%s%c%s", dtstr, ' ', tzname);
		PG_RETURN_CSTRING(resval);
}

static
TimestampTz GetTimestamp(void)
{
	TimestampTz result;
	struct timeval tp;

	gettimeofday(&tp, NULL);

	result = (TimestampTz) tp.tv_sec -
		((POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * SECS_PER_DAY);

#ifdef HAVE_INT64_TIMESTAMP
	result = (result * USECS_PER_SEC) + tp.tv_usec;
#else
	result = result + (tp.tv_usec / 1000000.0);
#endif

	return result;
}

/********************************************************************
 *
 * ora_systimestamp
 *
 * Purpose:
 *
 * get the system timestamp.
 *
 ********************************************************************/
Datum
ora_systimestamp(PG_FUNCTION_ARGS)
{
	PG_RETURN_TIMESTAMPTZ(GetTimestamp());
}

/********************************************************************
 *
 * ora_sys_extract_utc
 *
 * Purpose:
 *
 * Returns the UTC time.
 *
 ********************************************************************/
Datum
ora_sys_extract_utc(PG_FUNCTION_ARGS)
{
	TimestampTz timestamp = PG_GETARG_TIMESTAMPTZ(0);

	PG_RETURN_TIMESTAMPTZ(timestamp);
}

/********************************************************************
 *
 * function:day_diff
 *
 * Purpose:
 *
 * Calculate the number of days between dates.
 *
 ********************************************************************/
static int 
day_diff (int year_start, int month_start, int day_start, int year_end, int month_end, int day_end)
{
	int y2, m2, d2;
	int y1, m1, d1;
	/* Record the number of months between march */
	m1 = (month_start + 9) % 12;
	y1 = year_start - m1/10;
	/*Returns the days between the given time and 0000/3/1*/
	d1 = 365*y1 + y1/4 - y1/100 + y1/400 + (m1*306 + 5)/10 + (day_start - 1);

	m2 = (month_end + 9) % 12;
	y2 = year_end - m2/10;
	d2 = 365*y2 + y2/4 - y2/100 + y2/400 + (m2*306 + 5)/10 + (day_end - 1);

	return (d2 - d1);
}

/********************************************************************
 *
 * ora_days_between
 *
 * Purpose:
 *
 * Returns the time difference between two dates/times, in days, 
 * with the former parameter subtracted from the latter.
 *
 ********************************************************************/
Datum
ora_days_between(PG_FUNCTION_ARGS)
{

	Timestamp tmtz1 = PG_GETARG_TIMESTAMP(0);
	Timestamp tmtz2 = PG_GETARG_TIMESTAMP(1);

	int tz1, tz2;
	struct pg_tm tt1, *tm1 = &tt1;
	struct pg_tm tt2, *tm2 = &tt2;
	fsec_t fsec1, fsec2;
	const char *tzn1, *tzn2;
	int sod1, sod2, mondiff;

	if (TIMESTAMP_NOT_FINITE(tmtz1) || TIMESTAMP_NOT_FINITE(tmtz2))
	{
		return DirectFunctionCall3(numeric_in,
			CStringGetDatum("NaN"),
			ObjectIdGetDatum(InvalidOid),
			Int32GetDatum(-1));
	}

	if (timestamp2tm(tmtz1, &tz1, tm1, &fsec1, &tzn1, NULL) != 0)
		ereport(ERROR,
			(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
				errmsg("timestamp out of range")));

	if (timestamp2tm(tmtz2, &tz2, tm2, &fsec2, &tzn2, NULL) != 0)
		ereport(ERROR,
			(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
				errmsg("timestamp out of range")));

	mondiff = day_diff(tm2->tm_year,tm2->tm_mon,tm2->tm_mday,tm1->tm_year,tm1->tm_mon,tm1->tm_mday);
	sod1 = tm1->tm_hour * SECS_PER_HOUR
		+ tm1->tm_min * SECS_PER_MINUTE + tm1->tm_sec;
	sod2 = tm2->tm_hour * SECS_PER_HOUR
		+ tm2->tm_min * SECS_PER_MINUTE + tm2->tm_sec;

	{
		double f8_mondiff;
#ifdef HAVE_INT64_TIMESTAMP
		f8_mondiff = mondiff + ((sod1 - sod2)*USECS_PER_SEC + (fsec1 - fsec2))/(USECS_PER_DAY*1.0);
#else
		f8_mondiff = mondiff + ((sod1 - sod2) + (fsec1 - fsec2))/(SECS_PER_DAY*1.0);
#endif
		return DirectFunctionCall1(float8_numeric, Float8GetDatum(f8_mondiff));
	}
}

/********************************************************************
 *
 * ora_days_between_tmtz
 *
 * Purpose:
 *
 * Returns the time difference between two dates/times, in days, with the 
 * former parameter subtracted from the latter.
 *
 ********************************************************************/
Datum ora_days_between_tmtz(PG_FUNCTION_ARGS)
{
	TimestampTz tmtz1 = PG_GETARG_TIMESTAMPTZ(0);
	TimestampTz tmtz2 = PG_GETARG_TIMESTAMPTZ(1);

	int tz1, tz2;
	struct pg_tm tt1, *tm1 = &tt1;
	struct pg_tm tt2, *tm2 = &tt2;
	fsec_t fsec1, fsec2;
	const char *tzn1, *tzn2;
	int sod1, sod2, mondiff;

	if (TIMESTAMP_NOT_FINITE(tmtz1) || TIMESTAMP_NOT_FINITE(tmtz2))
	{
		return DirectFunctionCall3(numeric_in,
			CStringGetDatum("NaN"),
			ObjectIdGetDatum(InvalidOid),
			Int32GetDatum(-1));
	}

	if (timestamp2tm(tmtz1, &tz1, tm1, &fsec1, &tzn1, NULL) != 0)
		ereport(ERROR,
			(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
		 		errmsg("timestamp out of range")));

	if (timestamp2tm(tmtz2, &tz2, tm2, &fsec2, &tzn2, NULL) != 0)
		ereport(ERROR,
			(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
				errmsg("timestamp out of range")));

	mondiff = day_diff(tm2->tm_year,tm2->tm_mon,tm2->tm_mday,tm1->tm_year,tm1->tm_mon,tm1->tm_mday);
	sod1 = tm1->tm_hour * SECS_PER_HOUR
		+ tm1->tm_min * SECS_PER_MINUTE + tm1->tm_sec;
	sod2 = tm2->tm_hour * SECS_PER_HOUR
		+ tm2->tm_min * SECS_PER_MINUTE + tm2->tm_sec;
	{
		double f8_mondiff;
#ifdef HAVE_INT64_TIMESTAMP
		f8_mondiff = mondiff + ((sod1 - sod2)*USECS_PER_SEC + (fsec1 - fsec2))/(USECS_PER_DAY*1.0);
#else
		f8_mondiff = mondiff + ((sod1 - sod2) + (fsec1 - fsec2))/(SECS_PER_DAY*1.0);
#endif
		return DirectFunctionCall1(float8_numeric, Float8GetDatum(f8_mondiff));
	}

}
