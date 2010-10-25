/*
*				astrefcat.c
*
* Manage astrometric reference catalogs (query and load).
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*
*	This file part of:	SCAMP
*
*	Copyright:		(C) 2002-2010 Emmanuel Bertin -- IAP/CNRS/UPMC
*
*	License:		GNU General Public License
*
*	SCAMP is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
* 	(at your option) any later version.
*	SCAMP is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*	You should have received a copy of the GNU General Public License
*	along with SCAMP. If not, see <http://www.gnu.org/licenses/>.
*
*	Last modified:		10/10/2010
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#ifdef HAVE_CONFIG_H
#include	"config.h"
#endif

#include <math.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "define.h"
#include "globals.h"
#include "fits/fitscat.h"
#include "fitswcs.h"
#include "field.h"
#include "key.h"
#include "prefs.h"
#include "astrefcat.h"
#include "samples.h"

samplestruct	refsample;
keystruct       refkey[] = {
  {"X_WORLD", "Barycenter position along world x axis",
        &refsample.wcspos[0], H_FLOAT, T_DOUBLE, "%15e", "deg"},
  {"Y_WORLD", "Barycenter position along world y axis",
        &refsample.wcspos[1], H_FLOAT, T_DOUBLE, "%15e", "deg"},
  {"ERRA_WORLD", "World RMS position error along major axis",
        &refsample.wcsposerr[0], H_FLOAT, T_FLOAT, "%12e", "deg"},
  {"ERRB_WORLD", "World RMS position error along minor axis",
        &refsample.wcsposerr[1], H_FLOAT, T_FLOAT, "%12e", "deg"},
  {"MAG", "Generic magnitude",
        &refsample.mag, H_FLOAT, T_FLOAT, "%8.4f", "mag"},
  {"MAGERR", "Generic magnitude RMS error",
        &refsample.magerr, H_FLOAT, T_FLOAT, "%8.4f", "mag"},
  {""},
  };

astrefstruct	astrefcat[] = 
 {
  {"NONE", 0, 0, {""}},
  {"file", 12, 0, {"1","2","3","4","5","6","7","8","9","10","11","12",""}},
  {"USNO-A1.0", 2, 0, {"Bj", "Rf",""}, {"B", "R",""}},
  {"USNO-A2.0", 2, 0, {"Bj", "Rf",""}, {"B", "R",""}},
  {"USNO-B1.0", 3, 0, {"Bj", "Rf", "In",""}, {"B1", "R1", "I",""}},
  {"GSC-1.3", 1, 0, {"V",""}, {"V",""}},
  {"GSC-2.2", 4, 0, {"Bj", "V", "Rf", "In",""}, {"F", "J", "V", "N",""}},
  {"GSC-2.3", 6, 2, {"U", "B", "Bj", "V", "Rf", "In", ""},
			{"U", "B", "F", "J", "V", "N", ""}},
  {"2MASS", 3, 0, {"J", "H", "Ks",""}, {"J", "H", "K",""}},
  {"DENIS-3", 3, 0, {"i", "J", "Ks",""}, {"I", "J", "K",""}},
  {"UCAC-1", 1, 0, {"R",""}, {"R",""}},
  {"UCAC-2", 1, 0, {"R",""}, {"R",""}},
  {"UCAC-3", 1, 0, {"R",""}, {"R",""}},
  {"SDSS-R3", 5, 2, {"u", "g", "r", "i", "z",""}, {"u", "g", "r", "i", "z",""}},
  {"SDSS-R5", 5, 2, {"u", "g", "r", "i", "z",""}, {"u", "g", "r", "i", "z",""}},
  {"SDSS-R6", 5, 2, {"u", "g", "r", "i", "z",""}, {"u", "g", "r", "i", "z",""}},
  {"SDSS-R7", 5, 2, {"u", "g", "r", "i", "z",""}, {"u", "g", "r", "i", "z",""}},
  {"NOMAD-1.0", 6, 2, {"B", "V", "R", "J", "H", "Ks",""},
			{"B", "V", "R", "J", "H", "K",""}},
  {"PPMX", 7, 3, {"B", "V", "R", "Rf", "J", "H", "Ks",""},
			{"B", "V", "R", "C", "J", "H", "K",""}},
  {""}
 };

const char	astref_ctype[NAXIS][8]= {"RA---STG", "DEC--STG"};

const double	astref_crpix[NAXIS] = {8192.5, 8192.5},
		astref_cdelt[NAXIS] = {0.2*ARCSEC/DEG, 0.2*ARCSEC/DEG};

const int	astref_naxisn[NAXIS] = {16384, 16384};

/****** get_astreffield *********************************************************
PROTO   fieldstruct *get_astreffield(astrefenum refcat, double *wcspos,
				int lng, int lat, int naxis, double maxradius)
PURPOSE	Download reference catalog.
INPUT   Catalog name,
	Coordinate vector of the center,
	Longitude index,
	Latitude index,
	Number of axes (dimensions),
	Search radius (in degrees).
OUTPUT  Pointer to the reference field.
NOTES   Global preferences are used.
AUTHOR  E. Bertin (IAP)
VERSION 30/07/2010
*/
fieldstruct	*get_astreffield(astrefenum refcat, double *wcspos,
				int lng, int lat, int naxis, double maxradius)
  {
   fieldstruct	*field,*tfield;
   setstruct	*set;
   samplestruct	*sample;
   FILE		*file;
   char		*ctype[NAXIS],
		cmdline[MAXCHAR], str[MAXCHAR],sport[16],
		salpha[32],sdelta[32],
		smag[MAX_BAND][32],smagerr[MAX_BAND][32],sproperr[NAXIS][32],
		sflag[4],
		*bandname, *cdsbandname, *catname,
		flag1,flag2;
   double	poserr[NAXIS],prop[NAXIS],properr[NAXIS],
		mag[MAX_BAND],magerr[MAX_BAND], epoch,epocha,epochd,
		alpha,delta, dist, temp;
   int		b,c,d,i,n, nsample,nsamplemax, nobs, class, band, nband,
		maglimflag;

/* One needs 2 angular coordinates here! */
  if (naxis<2)
    return NULL;

  if (prefs.ref_port[0]==80 || prefs.ref_port[0]==0)
    sprintf(sport,"");
  else
    sprintf(sport, " %d", prefs.ref_port[0]);

/* If these are not angular coordinates, file mode becomes mandatory */
  if (lng == lat && refcat!=ASTREFCAT_FILE)
    {
    warning("Cartesian coordinates found: ",
	"reference catalog switched to FILE mode");
    refcat = ASTREFCAT_FILE;
    }

  catname = (char *)astrefcat[(int)refcat].name;
  nband = astrefcat[(int)refcat].nband;
  if (!cistrcmp(prefs.astref_bandname, "DEFAULT", FIND_STRICT))
    band = astrefcat[(int)refcat].defband;
  else if (!cistrcmp(prefs.astref_bandname, "BLUEST", FIND_STRICT))
    band = 0;
  else if (!cistrcmp(prefs.astref_bandname, "REDDEST", FIND_STRICT))
    band = nband-1;
  else if ((band=findkeys(prefs.astref_bandname,
		astrefcat[(int)refcat].bandnames, FIND_STRICT))==RETURN_ERROR)
    {
    sprintf(str, "%s: no such band in astrometric reference catalog %s, use ",
	prefs.astref_bandname, catname);
    for (b=0; b<nband; b++)
      {
      if (b)
        strcat(str, b==nband-1? " or " : ", ");
      strcat(str, astrefcat[(int)refcat].bandnames[b]);
      }
    error(EXIT_FAILURE, "*Error*: ", str);
    }
  astrefcat[(int)refcat].band = band;
  bandname = astrefcat[(int)refcat].bandname
	= astrefcat[(int)refcat].bandnames[band];
  cdsbandname = astrefcat[(int)refcat].cdsbandnames[band];
  maglimflag = (prefs.astref_maglim[0]>-99.0 || prefs.astref_maglim[1]<99.0)?
		1 : 0;

/* Call the right catalog */
  if (refcat==ASTREFCAT_FILE)
    {
    field = NULL;
    for (c=0; c<prefs.nastref_name; c++)
      {
      if ((tfield=load_astreffield(prefs.astref_name[c], wcspos, lng,lat,
		naxis, maxradius, band, prefs.astref_maglim)))
        {
        if (tfield)
          {
          NFPRINTF(OUTPUT, "");
          QPRINTF(OUTPUT, " %d astrometric references loaded from %s\n",
		tfield->set[0]->nsample, tfield->rfilename);
          }
        if (field)
          {
          union_samples(tfield->set[0]->sample, field->set[0],
		tfield->set[0]->nsample,
		ASTREF_ASSOCRADIUS, UNION_WCS);
          field->nsample = field->set[0]->nsample;
          end_field(tfield);
          }
        else field=tfield;
        }
      }
    if (!field)
        error(EXIT_FAILURE,"*Error*: No appropriate FITS-LDAC astrometric ",
			"reference catalog found");
    return field;
    }

  switch(refcat)
    {
    case ASTREFCAT_FILE:
/*---- Already exited at this point */
      break;
    case ASTREFCAT_USNOA1:
      if (maglimflag)
        sprintf(cmdline,
	"%s %s%s pmm1 -e7 -sr -c %s %s -r %16g -l%s %f,%f -m 10000000 -si",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else	
        sprintf(cmdline,
	"%s %s%s pmm1 -e7 -sr -c %s %s -r %16g -m 10000000 -si",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_USNOA2:
      if (maglimflag)
        sprintf(cmdline,
	"%s %s%s pmm2 -e7 -sr -c %s %s -r %16g -l%s %f,%f -m 10000000 -si",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline,
	"%s %s%s pmm2 -e7 -sr -c %s %s -r %16g -m 10000000 -si",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_USNOB1:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s usnob1 -c %s %s -r %16g -lm%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s usnob1 -c %s %s -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);

      break;
    case ASTREFCAT_GSC1:
      if (maglimflag)
        sprintf(cmdline,
	"%s %s%s gsc1.3 -c %s %s -r %16g -lm %f,%f  -n 10000000 -s 5",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline,
	"%s %s%s gsc1.3 -c %s %s -r %16g -n 10000000 -s 5",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_GSC22:
      if (maglimflag)
        sprintf(cmdline,
		"%s %s%s gsc2.2 -c %s %s -r %16g -l%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline,
		"%s %s%s gsc2.2 -c %s %s -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_GSC23:
      if (maglimflag)
        sprintf(cmdline,
		"%s %s%s gsc2.3 -c %s %s -r %16g -l%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline,
		"%s %s%s gsc2.3 -c %s %s -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_2MASS:
      if (maglimflag)
        sprintf(cmdline,
	"%s %s%s find2m -c %f12,%+f12 -r %16g -l%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline,
	"%s %s%s find2m -c %f12,%+f12 -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_DENIS3:
      if (maglimflag)
        sprintf(cmdline,
		"%s %s%s denis3 -c %f12,%+f12 -r %16g -lm%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline,
		"%s %s%s denis3 -c %f12,%+f12 -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_UCAC1:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s ucac1 -c %s %s -r %16g -lm %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s ucac1 -c %s %s -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_UCAC2:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s ucac2 -c %s %s -r %16g -lm %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s ucac2 -c %s %s -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_UCAC3:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s ucac3 -c %s %s -r %16g -lm %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s ucac3 -c %s %s -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_SDSSR3:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s sdss3 -c %f12 %+f12 -r %16g -lm%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s sdss3 -c %f12 %+f12 -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_SDSSR5:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s sdss5 -c %f12 %+f12 -r %16g -lm%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s sdss5 -c %f12 %+f12 -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_SDSSR6:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s sdss6 -c %f12 %+f12 -r %16g -lm%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s sdss6 -c %f12 %+f12 -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_SDSSR7:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s sdss7 -c %f12 %+f12 -r %16g -lm%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s sdss7 -c %f12 %+f12 -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_NOMAD1:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s nomad1 -c %f12,%+f12 -r %16g -lm%s %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		wcspos[lng], wcspos[lat],
		maxradius*DEG/ARCMIN,
		cdsbandname,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s nomad1 -c %f12,%+f12 -r %16g -m 10000000",
	      prefs.cdsclient_path,
	      prefs.ref_server[0],
	      sport,
	      wcspos[lng], wcspos[lat],
	      maxradius*DEG/ARCMIN);
      break;
    case ASTREFCAT_PPMX:
      if (maglimflag)
        sprintf(cmdline, "%s %s%s ppmx -c %s %s -r %16g -lm %f,%f -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN,
		prefs.astref_maglim[0],
		prefs.astref_maglim[1]);
      else
        sprintf(cmdline, "%s %s%s ppmx -c %s %s -r %16g -m 10000000",
		prefs.cdsclient_path,
		prefs.ref_server[0],
		sport,
		degtosexal(wcspos[lng], salpha), degtosexde(wcspos[lat], sdelta),
		maxradius*DEG/ARCMIN);
      break;
    default:
      return NULL;
    }

  strcat(cmdline, " 2>/dev/null");
  sprintf(str,"Querying %s at %s for astrometric reference stars...",
	catname,
	prefs.ref_server[0]);
  NFPRINTF(OUTPUT, str);
  QPOPEN(file, cmdline, "r");		/* popen() is POSIX.2 compliant */
  for (i=2; i--;)			/* Skip the first 2 lines */
    fgets(str, MAXCHAR, file);

  set = init_set();
  prop[lng] = prop[lat] = properr[lng] = properr[lat] = 0.0;
  n = nsample = nsamplemax = 0;

/* Now examine each entry */
  while (fgets(str, MAXCHAR, file))
    if (*str != '#')
      {
      switch(refcat)
        {
        case ASTREFCAT_USNOA1:
        case ASTREFCAT_USNOA2:
          sscanf(str, "%*13s %12s%12s%c%c %lf %lf %lf ; %lf",
		salpha, sdelta, &flag1, &flag2, &mag[0], &mag[1], &epoch,
		&dist);
          alpha = sextodegal(salpha);
          delta = sextodegde(sdelta);
          poserr[lat] = poserr[lng] = (refcat==ASTREFCAT_USNOA1)?
				USNOA1_POSERR : USNOA2_POSERR;
          magerr[0] = magerr[1] = (refcat==ASTREFCAT_USNOA1)?
			USNOA1_BMAGERR : USNOA2_BMAGERR;
          dist *= ARCSEC/DEG;
          break;

        case ASTREFCAT_USNOB1:
          sscanf(str, "%*25c %10s%10s %lf %lf %lf %lf %lf %*d %lf %lf"
			"%*d %*d %*d %3s|"
			"%5s%*26c%5s%*26c%s%*26c%s%*26c%s%*26c ; %lf",
		salpha, sdelta,
		&poserr[lng], &poserr[lat], &epoch,
		&prop[lng], &prop[lat], &properr[lng], &properr[lat],
		sflag, smag[0], smag[1], smag[2], smag[3], smag[4],
		&dist);
/*-------- Avoid spikes */
          if (sflag[0]=='s' || sflag[1]=='s' || sflag[2]=='s')
            continue;
          alpha = atof(salpha);
          delta = atof(sdelta);
          if (poserr[lng] != 999.0)
            poserr[lng] *= MAS/DEG;
          else
            poserr[lng] = USNOB1_POSERR;
          if (poserr[lat] != 999.0)
            poserr[lat] *= MAS/DEG;
          else
            poserr[lat] = USNOB1_POSERR;
          prop[lng] *= MAS/DEG;
          prop[lat] *= MAS/DEG;
          properr[lng] *= MAS/DEG;
          properr[lat] *= MAS/DEG;
          for (b=0; b<5; b++)	/* 5, not nband!*/ 
            if (*smag[b] == '-')
              mag[b] = magerr[b] = 99.0;
            else
              {
              mag[b] = atof(smag[b]);
              magerr[b] = USNOB1_BMAGERR;
	      }
/*-------- Merge B1 and B2, R1 and R2, shift In */
          if (mag[0] > 98.0)
            mag[0] = mag[2];
          if (mag[2] > 98.0)
            mag[2] = mag[0];
          mag[0] = (mag[0] + mag[2])*0.5;
          if (magerr[0] > 98.0)
            magerr[0] = magerr[2];
          if (magerr[2] > 98.0)
            magerr[2] = magerr[0];
          magerr[0] = (magerr[0] + magerr[2])*0.5;
          if (mag[1] > 98.0)
            mag[1] = mag[3];
          if (mag[3] > 98.0)
            mag[3] = mag[1];
          mag[1] = (mag[1] + mag[3])*0.5;
          if (magerr[1] > 98.0)
            magerr[1] = magerr[3];
          if (magerr[3] > 98.0)
            magerr[3] = magerr[1];
          magerr[1] = (magerr[1] + magerr[3])*0.5;
          mag[2] = mag[4];
          magerr[2] = magerr[4];
          dist *= ARCSEC/DEG;
          break;

        case ASTREFCAT_GSC1:
          sscanf(str, "%*s %lf %lf %lf %lf %lf %*d %d %*s %*s %lf %*f",
		&alpha, &delta, &poserr[lng], &mag[0], &magerr[0], &class,
		&dist);
          poserr[lng] *= ARCSEC/DEG;
          poserr[lat] = poserr[lng];
          dist *= ARCMIN/DEG;
          epoch = 1960.0;
          break;

        case ASTREFCAT_GSC22:
          sscanf(str,"%*s %lf %lf %lf %lf %lf %s ,%4c %s ,%4c %s ,%4c %s ,%4c"
		" %d %*f %*f %*f %*d ; %lf",
		&alpha, &delta, &epoch, &poserr[lng], &poserr[lat],
		smag[0],smagerr[0], smag[1],smagerr[1], smag[2],smagerr[2], 
		smag[3],smagerr[3],
		&class, &dist);
          if (class==5)
            continue;
          smagerr[0][4]=smagerr[1][4]=smagerr[2][4]=smagerr[3][4]='\0';
          for (b=0; b<nband; b++)
            if (*smag[b] == '-')
              mag[b] = magerr[b] = 99.0;
            else
              {
              mag[b] = atof(smag[b]);
              magerr[b] = atof(smagerr[b]);
              }
/*-------- Swap Bj with Rf and Rf with V to sort passbands by lambda */
          temp=mag[0];mag[0]=mag[1];mag[1]=temp;
          temp=magerr[0];magerr[0]=magerr[1];magerr[1]=temp;
          temp=mag[1];mag[1]=mag[2];mag[2]=temp;
          temp=magerr[1];magerr[1]=magerr[2];magerr[2]=temp;
          poserr[lng] *= ARCSEC/DEG;
          poserr[lat] *= ARCSEC/DEG;
          dist *= ARCSEC/DEG;
          break;

        case ASTREFCAT_GSC23:
/*-------- Remove the annoying '|' character */
          for (i=0; str[i]; i++)
            if (str[i] == '|')
              str[i] = ' ';
          sscanf(str, "%*33c %10s%10s %lf %lf %lf %s ,%4c %*s %s ,%4c %*s"
			" %s ,%4c %*s %s ,%4c %*s %s ,%4c %*s %s ,%4c %*s %d"
			" %*f %*f %*f ; %lf",
		salpha, sdelta,
		&poserr[lng], &poserr[lat], &epoch,
		smag[0], smagerr[0], smag[1], smagerr[1], 
		smag[2], smagerr[2], smag[3], smagerr[3], 
		smag[4], smagerr[4], smag[5], smagerr[5], 
		&class, &dist);
          if (class==5)
            continue;
          smagerr[0][4]=smagerr[1][4]=smagerr[2][4]=smagerr[3][4]
		=smagerr[4][4]=smagerr[4][4]='\0';
          alpha = atof(salpha);
          delta = atof(sdelta);
          poserr[lng] *= ARCSEC/DEG;
          poserr[lat] *= ARCSEC/DEG;
          for (b=0; b<nband; b++)
            if (*smag[b] == '-')
              mag[b] = magerr[b] = 99.0;
            else
              {
              mag[b] = atof(smag[b]);
              magerr[b] = atof(smagerr[b]);
              }
/*-------- Swap to sort passbands by lambda */
          temp=mag[0];mag[0]=mag[4];mag[4]=temp;
          temp=magerr[0];magerr[0]=magerr[4];magerr[4]=temp;
          temp=mag[1];mag[1]=mag[5];mag[5]=temp;
          temp=magerr[1];magerr[1]=magerr[5];magerr[5]=temp;
          temp=mag[3];mag[3]=mag[5];mag[5]=temp;
          temp=magerr[3];magerr[3]=magerr[5];magerr[5]=temp;
          dist *= ARCSEC/DEG;
          break;

        case ASTREFCAT_2MASS:
/*-------- Remove the annoying '|' character */
          for (i=0; str[i]; i++)
            if (str[i] == '|')
              str[i] = ' ';
          sscanf(str, "%lf %lf %lf %lf %*f %*s %s %s %*s %*s %s %s %*s %*s "
		"%s %s",
		&alpha, &delta, &poserr[lng], &poserr[lat],
		smag[0],  smagerr[0], smag[1], smagerr[1], smag[2],smagerr[2]);
          for (b=0; b<nband; b++)
            if (*smag[b] == '-' || *smagerr[b] == '-')
              mag[b] = magerr[b] = 99.0;
            else
              {
              mag[b] = atof(smag[b]);
              magerr[b] = atof(smagerr[b]);
              }
          poserr[lng] *= ARCSEC/DEG;
          poserr[lat] *= ARCSEC/DEG;
          dist = 0.0;
          break;

        case ASTREFCAT_DENIS3:
/*-------- Remove the annoying '|' character */
          for (i=0; str[i]; i++)
            if (str[i] == '|')
              str[i] = ' ';
          sscanf(str, "%*s %*s %*s %lf %lf %8c%5c%8c%5c%8c%5c",
		&alpha, &delta, 
		smag[0], smagerr[0], smag[1],smagerr[1], smag[2],smagerr[2]);
          smag[0][8]=smag[1][8]=smag[2][8]='\0';
          smagerr[0][5]=smagerr[1][5]=smagerr[2][5]='\0';
          for (b=0; b<nband; b++)
            if (smag[b][2] == ' ' || smagerr[b][2] == ' ')
              mag[b] = magerr[b] = 99.0;
            else
              {
              mag[b] = atof(smag[b]);
              magerr[b] = atof(smagerr[b]);
              }
          poserr[lng] = DENIS3_POSERR;
          poserr[lat] = poserr[lng];
          dist = 0.0;
          break;

        case ASTREFCAT_UCAC1:
          sscanf(str, "%*8c %11s%11s %lf %lf %lf %d %lf %lf %lf %lf %lf "
		"%*d ; %lf",
		salpha, sdelta,
		&poserr[lng], &poserr[lat], &mag[0], &nobs, &epoch,
		&prop[lng],&prop[lat], &properr[lng],&properr[lat],
		&dist);
/*-------- Avoid poor observations */
          if (nobs<2)
            continue;
          alpha = atof(salpha);
          delta = atof(sdelta);
          magerr[0] = 0.1;	/* Just a default value */
          poserr[lng] *= MAS/DEG;
          poserr[lat] *= MAS/DEG;
          prop[lng] *= MAS/DEG;
          prop[lat] *= MAS/DEG;
          properr[lng] *= MAS/DEG;
          properr[lat] *= MAS/DEG;
          dist *= ARCMIN/DEG;
          break;

        case ASTREFCAT_UCAC2:
          sscanf(str, "%*8c %11s%11s %*f %*f %lf %lf %d %*d %*d %lf "
		"%lf %lf %lf %lf %lf %*f %*f %*f %*f %*f %*f "
		"%*d %*d ; %lf",
		salpha, sdelta,
		&poserr[lng], &mag[0], &nobs, &epocha, &epochd,
		&prop[lng],&prop[lat], &properr[lng],&properr[lat],
		&dist);
/*-------- Avoid poor observations */
          if (nobs<2)
            continue;
          poserr[lat] = poserr[lng];
          epoch = 0.5*(epocha+epochd);
          alpha = atof(salpha);
          delta = atof(sdelta);
          magerr[0] = 0.1;	/* Just a default value */
          poserr[lng] *= MAS/DEG;
          poserr[lat] *= MAS/DEG;
          prop[lng] *= MAS/DEG;
          prop[lat] *= MAS/DEG;
          properr[lng] *= MAS/DEG;
          properr[lat] *= MAS/DEG;
          dist *= ARCMIN/DEG;
          break;

        case ASTREFCAT_UCAC3:
/*-------- Remove the annoying '|' character */
          for (i=0; str[i]; i++)
            if (str[i] == '|')
              str[i] = ' ';
          sscanf(str, "%*10c %11s%11s %*f %*f %lf %lf %lf %*f %s %s %*d %*d"
		"%d %*d%*d %*d %lf %lf %s %s "
		"%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s ; %lf",
		salpha, sdelta,
		&poserr[lng], &epocha, &epochd, smag[0], smagerr[0], &nobs,
		&prop[lng],&prop[lat], sproperr[lng],sproperr[lat], &dist);
/*-------- Avoid poor observations */
          if (nobs<2)
            continue;
          poserr[lat] = poserr[lng];
          epoch = 0.5*(epocha+epochd);
          alpha = atof(salpha);
          delta = atof(sdelta);
          mag[0] = atof(smag[0]);
          magerr[0] = (smagerr[0][0]=='-')? 0.9 : atof(smagerr[0]);
          properr[lng] = (sproperr[lng][0]=='-')? 1000.0 : atof(sproperr[0]);
          properr[lat] = (sproperr[lat][0]=='-')? 1000.0 : atof(sproperr[1]);
          prop[lng] *= MAS/DEG;
          prop[lat] *= MAS/DEG;
          properr[lng] *= MAS/DEG;
          properr[lat] *= MAS/DEG;
          poserr[lng] *= MAS/DEG;
          poserr[lat] *= MAS/DEG;
          dist *= ARCMIN/DEG;
          break;

        case ASTREFCAT_SDSSR3:
          sscanf(str, "%*24c %10s%10s %lf %d %lf`%lf %lf`%lf %lf`%lf %lf`%lf"
			" %lf`%lf ; %lf",
		salpha, sdelta,
		&epoch, &nobs,
		&mag[0],&magerr[0],&mag[1],&magerr[1],&mag[2],&magerr[2],
		&mag[3],&magerr[3],&mag[4],&magerr[4],&dist);
/*-------- Avoid missing or poor observations */
          if (nobs<2 || nobs>3)
            continue;
          alpha = atof(salpha);
          delta = atof(sdelta);
          poserr[lng] = poserr[lat] = SDSSR3_POSERR;
          dist *= ARCMIN/DEG;
          break;

        case ASTREFCAT_SDSSR5:
        case ASTREFCAT_SDSSR6:
        case ASTREFCAT_SDSSR7:
          sscanf(str, "%*24c %*2c %10s%10s %lf %lf %*f %lf %d %*s %lf`%lf "
			"%lf`%lf %lf`%lf %lf`%lf %lf`%lf ; %lf",
		salpha, sdelta,
		&poserr[lng], &poserr[lat],
		&epoch, &nobs,
		&mag[0],&magerr[0],&mag[1],&magerr[1],&mag[2],&magerr[2],
		&mag[3],&magerr[3],&mag[4],&magerr[4],&dist);
/*-------- Avoid missing or poor observations */
          if (nobs<2 || nobs>3)
            continue;
          alpha = atof(salpha);
          delta = atof(sdelta);
          poserr[lng] *= ARCSEC/DEG;
          poserr[lat] *= ARCSEC/DEG;
          dist *= ARCMIN/DEG;
          break;

        case ASTREFCAT_NOMAD1:
	  /*-------- Remove the annoying '|' character */
          for (i=0; str[i]; i++)
            if (str[i] == '|')
              str[i] = ' ';
	  sscanf(str, "%*s %*s %11s%11s %*s %lf %lf %lf %*f %*f %*f %*f %*s "
		 "%s %s %s %s %s %s %lf",
		 salpha, sdelta, &poserr[lng], &poserr[lat], &epoch,
		 smag[0], smag[1],smag[2],smag[3],smag[4],smag[5], &dist);
          alpha = atof(salpha);
          delta = atof(sdelta);
          for (b=0; b<nband; b++)
            if (*smag[b] == '-')
              mag[b] = magerr[b] = 99.0;
            else
              {
              smag[b][6] = '\0';
              mag[b] = atof(smag[b]);
              magerr[b] = NOMAD1_MAGERR;
              }
          poserr[lng] *= MAS/DEG;
          poserr[lat] *= MAS/DEG;
          dist *= ARCMIN/DEG;
          break;

        case ASTREFCAT_PPMX:
	  /*-------- Remove the annoying '|' character */
          for (i=0; str[i]; i++)
            if (str[i] == '|')
              str[i] = ' ';
	  sscanf(str, "%*s %10s%10s %lf %lf %lf %lf %lf %lf %lf %lf"
		 " %s %s %s %s %s %s %s %s %s %s %s %3c%*21c ; %lf",
		salpha, sdelta, &prop[lng], &prop[lat], &epocha, &epochd,
		&poserr[lng], &poserr[lat], &properr[lng], &properr[lat],
		smag[3],smag[2], smag[0],smagerr[0], smag[1],smagerr[1],
		smag[4],smagerr[4], smag[5],smagerr[5], smag[6],smagerr[6],
		&dist);
          alpha = atof(salpha);
          delta = atof(sdelta);
          epoch = 0.5*(epocha+epochd);
          poserr[lng] *= MAS/DEG;
          poserr[lat] *= MAS/DEG;
          prop[lng] *= MAS/DEG;
          prop[lat] *= MAS/DEG;
          properr[lng] *= MAS/DEG;
          properr[lat] *= MAS/DEG;
          smagerr[6][3] = '\0';
          for (b=0; b<nband; b++)
            if (*smag[b] == '-')
              mag[b] = magerr[b] = 99.0;
            else
              {
              mag[b] = atof(smag[b]);
              magerr[b] = (b!=2 && b!=3)? atof(smagerr[b])*0.001 : GSC_MAGERR;
              }
          dist *= ARCSEC/DEG;
          break;

        case ASTREFCAT_NONE:
        default:
          break;
        }

      if (!(n%10000))
        {
        sprintf(str,"Catalog %s (%s band) : Object #%d / %d samples stored",
		catname,bandname, n,nsample);
        NFPRINTF(OUTPUT, str);
        }
/*------ Apply some selection over flags, fluxes,.. */
      if (1)
        {
/*------ Allocate memory for the first shipment */
        if (!set->sample)
          {
          nsample = 0;
          nsamplemax = LSAMPLE_DEFSIZE;
          malloc_samples(set, nsamplemax);
          }

/*------ Increase storage space to receive new candidates if needed */
        if (nsample>=nsamplemax)
          {
           int	nadd=(int)(1.62*nsamplemax);
          nsamplemax = nadd>nsamplemax?nadd:nsamplemax+1;
          realloc_samples(set, nsamplemax);
          }

        sample = set->sample + nsample;
        if (mag[band] < 98.0)
          {
          sample->mag = mag[band];
          sample->magerr = magerr[band];
          }
        else
          {
          for (b=0; b<nband; b++)
            if (mag[b] < 98.0)
              {
              sample->mag = mag[b];
              sample->magerr = magerr[b];
              break;
              }
/*-------- If no magnitude is suitable, drop this source */
          if (mag[b] > 98.0)
            continue;
          }
        if (nband>1 && mag[0]<98.0 && mag[1]<98.0)
          sample->colour = mag[0] - mag[1];
        else
          sample->colour = 99.0;
        sample->flux = 0.0;
        sample->wcspos[lng] = alpha;
        sample->wcspos[lat] = delta;
        sample->wcsposerr[lng] = poserr[lng];
        sample->wcsposerr[lat] = poserr[lat];
        sample->wcsprop[lng] = prop[lng];
        sample->wcsprop[lat] = prop[lat];
        sample->wcsproperr[lng] = properr[lng];
        sample->wcsproperr[lat] = properr[lat];
        sample->flags = 0;	/* Flags are not relevant for ref. sources*/
        sample->set = set;
        nsample++;
        }
      n++;
      }

  fclose(file);

/* Escape here if no source returned */
  if (!nsample)
    return NULL;

  set->nsample = nsample;

/* Don't waste memory! */
  realloc_samples(set, nsample);

/* Now "create" the field */
  QCALLOC(field, fieldstruct, 1);
  QMALLOC(field->set, setstruct *, 1);
  field->set[0] = set;
  field->nset = 1;
/* This is a reference catalog */
  field->astromlabel = field->photomlabel = -1;
  set->lng = field->lng = lng;
  set->lat = field->lat = lat;
  set->naxis = field->naxis = naxis;
  field->maxradius = set->radius = maxradius;
  set->field = field;
  for (d=0; d<naxis; d++)
    {
    set->wcspos[d] = field->meanwcspos[d] = wcspos[d];
    ctype[d] = (char *)astref_ctype[d];
    }

/* Create a dummy WCS structure */
  set->wcs = create_wcs(ctype, wcspos, (double *)astref_crpix,
			(double *)astref_cdelt,
			(int *)astref_naxisn, naxis);

  field->nsample = set->nsample;

  return field;
  }


/****** save_astreffield ******************************************************
PROTO   void save_astreffield(char *filename, fieldstruct *reffield)
PURPOSE	Save an astrometric reference catalog in (pseudo-)LDAC format.
INPUT   Catalog name,
	pointer to the reference field to save.
OUTPUT  -.
NOTES   Global preferences are used.
AUTHOR  E. Bertin (IAP)
VERSION 22/10/2009
*/
void	save_astreffield(char *filename,  fieldstruct *reffield)
  {
   static char	imtabtemplate[][80] = {
"SIMPLE  =                    T / This is a FITS file",
"BITPIX  =                    8 / ",
"NAXIS   =                    2 / 2D data",
"NAXIS1  =                    1 / Number of rows",
"NAXIS2  =                    1 / Number of columns",
"EXTEND  =                    T / This file may contain FITS extensions",
"END                            "};
  catstruct	*cat;
  tabstruct	*asctab, *imtab, *objtab;
  keystruct	*key, *objkeys;
  setstruct	*set;
  samplestruct	*sample,
		objsample;
  char		*buf;
  long		dptr;
  int		i,k,n,s;

/* Create a new output catalog */
  cat = new_cat(1);
  init_cat(cat);
  strcpy(cat->filename, filename);
  if (open_cat(cat, WRITE_ONLY) != RETURN_OK)
    error(EXIT_FAILURE, "*Error*: cannot open for writing ", filename);

/* Primary header */
  save_tab(cat, cat->tab);

  for (s=0; s<reffield->nset; s++)
    {
    set = reffield->set[s];
/*-- We create a dummy table (only used through its header) */
    QCALLOC(asctab, tabstruct, 1);
    asctab->headnblock = 1 + (sizeof(imtabtemplate)-1)/FBSIZE;
    QCALLOC(asctab->headbuf, char, asctab->headnblock*FBSIZE);
    memcpy(asctab->headbuf, imtabtemplate, sizeof(imtabtemplate));
    for (buf = asctab->headbuf, i=FBSIZE*asctab->headnblock; i--; buf++)
      if (!*buf)
        *buf = ' ';
    write_wcs(asctab, set->wcs);
/*-- (dummy) LDAC Image header */

    imtab = new_tab("LDAC_IMHEAD");
    key = new_key("Field Header Card");
    key->ptr = asctab->headbuf;
    asctab->headbuf = NULL;
    free_tab(asctab);
    key->naxis = 2;
    QMALLOC(key->naxisn, int, key->naxis);
    key->naxisn[0] = 80;
    key->naxisn[1] = 36;
    key->htype = H_STRING;
    key->ttype = T_STRING;
    key->nobj = 1;
    key->nbytes = 80*(fitsfind(key->ptr, "END     ")+1);
    add_key(key, imtab, 0);
    save_tab(cat, imtab);
    free_tab(imtab);

/*-- LDAC Object header */
    objtab = new_tab("LDAC_OBJECTS");
    objtab->cat = cat;
/*-- Set key pointers */
    QCALLOC(objkeys, keystruct, (sizeof(refkey) / sizeof(keystruct)));
    dptr = (long)((char *)&objsample - (char *)&refsample);
    for (k=0; refkey[k].name[0]; k++)
      {
      objkeys[k] = refkey[k];
      objkeys[k].ptr = (void *)((char *)objkeys[k].ptr + dptr); /* a trick */
      add_key(&objkeys[k],objtab, 0);
      }
    init_writeobj(cat, objtab, &buf);
    sample = set->sample;   
    for (n=set->nsample; n--;)
      {
      objsample = *(sample++);
      write_obj(objtab, buf);
      }
    end_writeobj(cat, objtab, buf);
    objtab->key = NULL;
    objtab->nkey = 0;
    free_tab(objtab);
    free(objkeys);
    }

  free_cat(&cat, 1);

  return;
  }

/****** load_astreffield ******************************************************
PROTO   fieldstruct *load_astreffield(char *filename, double *wcspos,
			int lng, int lat, int naxis, double maxradius, int band,
			double *maglim)
PURPOSE	Load a reference catalog in (pseudo-)LDAC format.
INPUT   Catalog name,
	pointer to the field center coordinates,
	longitude index,
	latitude index,
	number of axes (dimensions),
	search radius (in degrees),
	band index,
	magnitude range.
OUTPUT  Pointer to the reference catalog field structure.
NOTES   Global preferences are used.
AUTHOR  E. Bertin (IAP)
VERSION 02/10/2009
*/
fieldstruct	*load_astreffield(char *filename, double *wcspos,
				int lng, int lat,
				int naxis, double maxradius, int band,
				double *maglim)
  {
   catstruct	*cat;
   tabstruct	*tab;
   fieldstruct	*field;
   setstruct	*set;
   char		str[MAXCHAR],
		*rfilename, *pstr, *pspath;
   int		i, n, nsample;


/* A short, "relative" version of the filename */
  if (!(rfilename = strrchr(filename, '/')))
    rfilename = filename;
  else
    rfilename++;

  sprintf(str,"Examining Catalog %s...", rfilename);
  NFPRINTF(OUTPUT, str);

/*-- Read input catalog */
  if (!(cat = read_cat(filename)))
    error(EXIT_FAILURE, "*Error*: No such catalog: ", filename);
  QCALLOC(field, fieldstruct, 1);
  QMALLOC(field->set, setstruct *, 1);
  strcpy (field->filename, filename);
  field->rfilename = rfilename;

/* Create a file name with a "header" extension */
  strcpy(field->hfilename, filename);
  if (!(pstr = strrchr(field->hfilename, '.')))
    pstr = field->hfilename+strlen(field->hfilename);
  sprintf(pstr, "%s", prefs.ahead_suffix);

/* Extract the path from the filename */
#ifdef HAVE_GETENV
  pspath = getenv("PWD");
#else
  pspath = NULL;
#endif
  if (*field->filename == '/')
    strcpy(field->path, field->filename);
  else
    {
    strcpy(field->path, pspath? pspath: ".");
    if (*field->filename != '.' && (pstr = strchr(field->filename, '/')))
      {
      strcat(field->path, "/");
      strcat(field->path, pstr+1);
      }
    }
  if ((pstr = strrchr(field->path, '/')))
    *pstr = '\0';

/* Identify image headers in catalog  */
  tab = cat->tab;
 
  n = 0;

/* Find the object table */
  sprintf(str,"Loading Catalog %s...", rfilename);
  NFPRINTF(OUTPUT, str);
  tab = cat->tab;
  set = NULL;
  nsample = 0;
  n = 0;
  for (i=cat->ntab; i--; tab=tab->nexttab)
    if (!strcmp("LDAC_OBJECTS", tab->extname)
	|| !strcmp("OBJECTS", tab->extname))
    {
    if (field->nset>1)
      sprintf(str, "%s [%d]", rfilename, n+1);
    else
      strcpy(str, rfilename);
    set = read_astrefsamples(set,tab, str, wcspos,lng,lat,naxis,maxradius,band,
			maglim);
    nsample += set->nsample;
    n++;
    }

  field->nsample = nsample;
  free_cat(&cat, 1);

  if (!n)
    {
/*-- No source found: return a NULL pointer */
    end_field(field);
    return NULL;
    }

/* This is a reference catalog */
  field->astromlabel = field->photomlabel = -1;
  field->set[0] = set;
  field->nset = 1;
  set->lng = field->lng = lng;
  set->lat = field->lat = lat;
  set->naxis = field->naxis = naxis;
  set->radius = maxradius;
  set->field = field;

  field->nsample = set->nsample;

  return field;
  }


/****** read_astrefsamples ****************************************************
PROTO	setstruct *read_astrefsamples(setstruct *set, tabstruct *tab,
				char *rfilename,
				double *wcspos, int lng, int lat, int naxis,
				double maxradius, int band, double *maglim)
PURPOSE	Read a set of astrometric reference samples.
INPUT	Set structure pointer,
	Pointer to the tab that contains the catalog,
        Reduced filename.
	Coordinate vector of the center,
	Longitude index,
	Latitude index,
	Number of axes (dimensions),
	Search radius (in degrees),
	band index,
	magnitude range.
OUTPUT  setstruct pointer (allocated if the input setstruct pointer is NULL).
NOTES   The filename is used for error messages only. Global preferences are
	used.
AUTHOR  E. Bertin (IAP)
VERSION 02/10/2009
*/
setstruct *read_astrefsamples(setstruct *set, tabstruct *tab, char *rfilename,
				double *wcspos, int lng, int lat, int naxis,
				double maxradius, int band, double *maglim)


  {
   tabstruct		*keytab;
   keystruct		*key;
   samplestruct		*sample;
   char			str[MAXCHAR];
   char			*buf;
   unsigned short	*flags;
   float		*xm,*ym, *mag, *erra,*errb;
   double		*dxm, *dym, *dmag, *derra, *derrb,
			x,y, dx,dy,dfac, ea,eb, maxradius2, mmag;
   int			n, nsample,nsamplemax, nobj, objflags, maglimflag;

/* One needs 2 angular coordinates here! */
  dxm = dym = dmag = derra = derrb = NULL;
  xm = ym = mag = erra = errb = NULL;
  maxradius2 = maxradius*maxradius;
  dfac = (lng!=lat)? cos(wcspos[lat]*DEG) : 1.0;

/* If a NULL pointer is provided, we allocate a new set */
  if (!set)
    {
    set = init_set();
    nsample = nsamplemax = 0;
    }
  else
    nsample = nsamplemax = set->nsample;

/* Init the single-row tab */
  keytab = init_readobj(tab, &buf);

  if (!(key = name_to_key(keytab, prefs.astrefcent_key[0])))
    {
    sprintf(str, "*Error*: %s parameter not found in catalog ",
		prefs.astrefcent_key[0]);
    error(EXIT_FAILURE, str, rfilename);
    }
  if (key->ttype == T_DOUBLE)
    dxm = (double *)key->ptr;
  else
    xm = (float *)key->ptr;

  nobj = key->nobj;

  if (!(key = name_to_key(keytab, prefs.astrefcent_key[1])))
    {
    sprintf(str, "*Error*: %s parameter not found in catalog ",
		prefs.astrefcent_key[1]);
    error(EXIT_FAILURE, str, rfilename);
    }
  if (key->ttype == T_DOUBLE)
    dym = (double *)key->ptr;
  else
    ym = (float *)key->ptr;

  if (!(key = name_to_key(keytab, prefs.astreferr_key[0])))
    {
    sprintf(str, "*Error*: %s parameter not found in catalog ",
		prefs.astreferr_key[0]);
    error(EXIT_FAILURE, str, rfilename);
    }
  if (key->ttype == T_DOUBLE)
    derra = (double *)key->ptr;
  else
    erra = (float *)key->ptr;

  if (!(key = name_to_key(keytab, prefs.astreferr_key[1])))
    {
    sprintf(str, "*Error*: %s parameter not found in catalog ",
		prefs.astreferr_key[1]);
    error(EXIT_FAILURE, str, rfilename);
    }
  if (key->ttype == T_DOUBLE)
    derrb = (double *)key->ptr;
  else
    errb = (float *)key->ptr;

  if (!(key = name_to_key(keytab, prefs.astrefmag_key)))
    {
    sprintf(str, "*Error*: %s parameter not found in catalog ",
		prefs.astrefmag_key);
    error(EXIT_FAILURE, str, rfilename);
    }
  if (key->ttype == T_DOUBLE)
    dmag = (double *)key->ptr;
  else
    mag = (float *)key->ptr;

/* Check that catalog contains enough bands if needed */
  if (band && (!key->naxis || band>=*key->naxisn))
    {
    sprintf(str, "*Error*: band #%d not found in catalog ", band+1);
    error(EXIT_FAILURE, str, rfilename);
    }

  if (!(key = name_to_key(keytab, "FLAGS")))
    warning("FLAGS parameter not found in catalog ", rfilename);
  flags = key? (unsigned short *)key->ptr : NULL;

/* Check magnitude limits only if needed */
  maglimflag = (maglim[0]>-99.0 || maglim[1]<99.0)? 1 : 0;

/* Now examine each vector of the shipment */
  for (n=0; nobj--; n++)
    {
    objflags = 0;
    read_obj(keytab,tab, buf);
    if (!(n%10000))
      {
      sprintf(str,"Catalog %s: Object #%d / %d samples stored",
		rfilename,n,nsample);
      NFPRINTF(OUTPUT, str);
      }
/*---- Apply some selection over flags, fluxes... */
    mmag = mag? *mag : *dmag;
    if (maglimflag && (mmag<maglim[0] || mmag>maglim[1]))
      continue;
    if (flags)
      {
      if (*flags & prefs.flags_mask)
        continue;
/*---- Mapping from SExtractor flags is straightforward */
      objflags = *flags & (OBJ_CROWDED|OBJ_MERGED|OBJ_SATUR);
      if (objflags & 4)		/* A saturated object */
        set->nsaturated++;
      }
    x = fmod((xm? *xm : *dxm) +360.0,360.0);
    y = ym? *ym : *dym;
    dx = x-wcspos[lng];
    if (dx>180.0)
      dx -= 360.0;
    else if (dx<-180.0)
      dx += 360.0;
    dx *= dfac;
    dy = y-wcspos[lat];
    if (dx*dx+dy*dy > maxradius2)
      continue;
/*-- ... and check the integrity of the sample */
/*-- Allocate memory for the first shipment */
    if (!set->sample)
      {
      nsample = 0;
      nsamplemax = LSAMPLE_DEFSIZE;
      malloc_samples(set, nsamplemax);
      }

/*-- Increase storage space to receive new candidates if needed */
    if (nsample>=nsamplemax)
      {
       int	nadd=(int)(1.62*nsamplemax);
      nsamplemax = nadd>nsamplemax?nadd:nsamplemax+1;
      realloc_samples(set, nsamplemax);
      }

    sample = set->sample + nsample;
    sample->set = set;
    sample->flags = objflags;
    sample->mag = mag? mag[band] : dmag[band];
    sample->flux = 0.0;
    sample->wcspos[lng] = x;
    sample->wcspos[lat] = y;
    ea = erra? *erra : *derra;
    eb = errb? *errb : *derrb;
    sample->wcsposerr[lng] = sample->wcsposerr[lat] = sqrt(ea*ea+eb*eb);
/*-- In case of a contamination, position errors are easily doubled */
    if (flags && *flags>0)
      sample->wcsposerr[lng] = (sample->wcsposerr[lat] *= 2.0);
    nsample++;
    }

  end_readobj(keytab, tab, buf);

  set->nsample = nsample;

/* Don't waste memory! */
  if (nsample)
    realloc_samples(set, nsample);

  return set;
  }

