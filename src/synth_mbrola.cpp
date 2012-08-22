/***************************************************************************
 *   Copyright (C) 2007 by Jonathan Duddington                             *
 *   jonsd@users.sourceforge.net                                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "StdAfx.h"

#include <stdio.h>
#include <ctype.h>
#include <wctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "speak_lib.h"
#include "speech.h"
#include "phoneme.h"
#include "synthesize.h"
#include "translate.h"
#include "voice.h"


#ifdef USE_MBROLA_DLL
#include <windows.h>
#include "mbrola.h"

extern int event_list_ix;
extern long count_samples;
extern unsigned char *outbuf;
extern int outbuf_size;
extern espeak_EVENT *event_list;
extern t_espeak_callback* synth_callback;

HINSTANCE	hinstDllMBR = NULL;
PROCIC		init_MBR;
PROCICCC	init_rename_MBR;
PROCIC		write_MBR;
PROCIV		flush_MBR;
PROCISI		read_MBR;
PROCVV		close_MBR;
PROCVV		reset_MBR;
PROCIV		lastError_MBR;
PROCVCI		lastErrorStr_MBR;
PROCVI		setNoError_MBR;
PROCVF		setVolumeRatio_MBR;


BOOL load_MBR()
{
	if(hinstDllMBR != NULL)
		return TRUE;   // already loaded 

	if (!(hinstDllMBR=LoadLibrary("mbrola.dll")))
		return FALSE;
	init_MBR			=(PROCIC) GetProcAddress(hinstDllMBR,"init_MBR");
	write_MBR			=(PROCIC) GetProcAddress(hinstDllMBR,"write_MBR");
	flush_MBR			=(PROCIV) GetProcAddress(hinstDllMBR,"flush_MBR");
	read_MBR			=(PROCISI) GetProcAddress(hinstDllMBR,"read_MBR");
	close_MBR			=(PROCVV) GetProcAddress(hinstDllMBR,"close_MBR");
	reset_MBR			=(PROCVV) GetProcAddress(hinstDllMBR,"reset_MBR");
	lastError_MBR		=(PROCIV) GetProcAddress(hinstDllMBR,"lastError_MBR");
	lastErrorStr_MBR	=(PROCVCI) GetProcAddress(hinstDllMBR,"lastErrorStr_MBR");
	setNoError_MBR		=(PROCVI) GetProcAddress(hinstDllMBR,"setNoError_MBR");
	setVolumeRatio_MBR	=(PROCVF) GetProcAddress(hinstDllMBR,"setVolumeRatio_MBR");
	return TRUE;
}


void unload_MBR()
{
	if (hinstDllMBR)
	{
		FreeLibrary (hinstDllMBR);
		hinstDllMBR=NULL;
	}
}

#endif


MBROLA_TAB *mbrola_tab = NULL;
int mbrola_control = 0;




espeak_ERROR LoadMbrolaTable(const char *mbrola_voice, const char *phtrans)
{//========================================================================
// Load a phoneme name translation table from espeak-data/mbrola

	int size;
	FILE *f_in;
	char path[150];

	mbrola_name[0] = 0;
	if(mbrola_voice == NULL)
	{
		samplerate = samplerate_native;
		SetParameter(espeakVOICETYPE,0,0);
		return(EE_OK);
	}

	sprintf(path,"%s/mbrola/%s",path_home,mbrola_voice);
#ifdef USE_MBROLA_DLL
	if(load_MBR() == FALSE)     // load mbrola.dll
		return(EE_INTERNAL_ERROR); 

	if(init_MBR(path) != 0)      // initialise the required mbrola voice
		return(EE_NOT_FOUND);

	setNoError_MBR(1);     // don't stop on phoneme errors
#endif

	// read eSpeak's mbrola phoneme translation data, eg. en1_phtrans
	sprintf(path,"%s/mbrola_ph/%s",path_home,phtrans);
	size = GetFileLength(path);
	if((f_in = fopen(path,"r")) == NULL)
		return(EE_NOT_FOUND);

	if((mbrola_tab = (MBROLA_TAB *)realloc(mbrola_tab,size)) == NULL)
	{
		fclose(f_in);
		return(EE_INTERNAL_ERROR);
	}
	fread(&mbrola_control,4,1,f_in);
	fread(mbrola_tab,size,1,f_in);
	fclose(f_in);

#ifdef USE_MBROLA_DLL
	setVolumeRatio_MBR((float)(mbrola_control & 0xff) /16.0);
#endif

	option_quiet = 1;
	samplerate = 16000;
	strcpy(mbrola_name,mbrola_voice);
	SetParameter(espeakVOICETYPE,1,0);
	return(EE_OK);
}


int GetMbrName(PHONEME_LIST *plist, PHONEME_TAB *ph, PHONEME_TAB *ph_prev, PHONEME_TAB *ph_next, int *name2, int *split, int *control)
{//==============================================================================================================
// Look up a phoneme in the mbrola phoneme name translation table
// It may give none, 1, or 2 mbrola phonemes
	int mnem = ph->mnemonic;
	MBROLA_TAB *pr;
	PHONEME_TAB *other_ph;
	int found = 0;

	// control
	// bit 0  skip the next phoneme
	// bit 1  match this and Previous phoneme
	// bit 2  only at the start of a word

	pr = mbrola_tab;
	while(pr->name != 0)
	{
		if(mnem == pr->name)
		{
			if(pr->next_phoneme == 0)
				found = 1;
			else
			{
				if(pr->control & 2)
					other_ph = ph_prev;
				else
					other_ph = ph_next;

				if((pr->next_phoneme == other_ph->mnemonic) ||
					((pr->next_phoneme == 2) && (other_ph->type == phVOWEL)) ||
					((pr->next_phoneme == '_') && (other_ph->type == phPAUSE)))
				{
					found = 1;
				}
			}

			if((pr->control & 4) && (plist->newword == 0))  // only at start of word
				found = 0;

			if(found)
			{
				*name2 = pr->mbr_name2;
				*split = pr->percent;
				*control = pr->control;
				return(pr->mbr_name);
			}
		}

		pr++;
	}
	*name2=0;
	*split=0;
	*control=0;
	return(mnem);
}


static char *WritePitch(int env, int pitch1, int pitch2, int split)
{//================================================================
	int x;
	int ix;
	int pitch_base;
	int pitch_range;
	int p1,p2,p_end;
	unsigned char *pitch_env;
	int max = -1;
	int min = 999;
	int y_max=0;
	int y_min=0;
	int y2;
	int y[4];
	int env_split;
	char buf[40];
	static char output[40];

	output[0] = 0;
	pitch_env = envelope_data[env];

	if(pitch1 > pitch2)
	{
		x = pitch1;   // swap values
		pitch1 = pitch2;
		pitch2 = x;
	}

	pitch_base = voice->pitch_base + (pitch1 * voice->pitch_range);
	pitch_range = voice->pitch_base + (pitch2 * voice->pitch_range) - pitch_base;

	env_split = (split * 128)/100;
	if(env_split < 0)
		env_split = 0-env_split;

	// find max and min in the pitch envelope
	for(x=0; x<128; x++)
	{
		if(pitch_env[x] > max)
		{
			max = pitch_env[x];
			y_max = x;
		}
		if(pitch_env[x] < min)
		{
			min = pitch_env[x];
			y_min = x;
		}
	}
	// set an additional pitch point half way through the phoneme.
	// but look for a maximum or a minimum and use that instead
	y[2] = 64;
	if((y_max > 0) && (y_max < 127))
	{
		y[2] = y_max;
	}
	if((y_min > 0) && (y_min < 127))
	{
		y[2] = y_min;
	}
	y[1] = y[2] / 2;
	y[3] = y[2] + (127 - y[2])/2;

	// set initial pitch
	p1 = ((pitch_env[0]*pitch_range)>>8) + pitch_base;   // Hz << 12
	p_end = ((pitch_env[127]*pitch_range)>>8) + pitch_base;


	if(split >= 0)
	{
		sprintf(buf," 0 %d",p1/4096);
		strcat(output,buf);
	}

	// don't use intermediate pitch points for linear rise and fall
	if(env > 1)
	{
		for(ix=1; ix<4; ix++)
		{
			p2 = ((pitch_env[y[ix]]*pitch_range)>>8) + pitch_base;

			if(split > 0)
			{
				y2 = (y[ix] * 100)/env_split;
			}
			else
			if(split < 0)
			{
				y2 = ((y[ix]-env_split) * 100)/env_split;
			}
			else
			{
				y2 = (y[ix] * 100)/128;
			}
			if((y2 > 0) && (y2 <= 100))
			{
				sprintf(buf," %d %d",y2,p2/4096);
				strcat(output,buf);
			}
		}
	}

	if(split <= 0)
	{
		sprintf(buf," 100 %d",p_end/4096);
		strcat(output,buf);
	}
	strcat(output,"\n");
	return(output);
}  // end of SetPitch


#ifdef USE_MBROLA_DLL

static void MbrolaMarker(int type, int char_posn, int length, int value)
{//=====================================================================

	MarkerEvent(type,(char_posn & 0xffffff) | (length << 24),value,outbuf);

}


static void MbrolaEmbedded(int &embix, int sourceix)
{//=================================================
	// There were embedded commands in the text at this point
	unsigned int word;  // bit 7=last command for this word, bits 5,6 sign, bits 0-4 command
	unsigned int value;
	int command;

	do {
		word = embedded_list[embix++];
		value = word >> 8;
		command = word & 0x7f;

		switch(command & 0x1f)
		{
		case EMBED_M:   // named marker
			MbrolaMarker(espeakEVENT_MARK, (sourceix & 0x7ff) + clause_start_char, 0, value);
			break;
		}
	} while ((word & 0x80) == 0);
}



int MbrolaSynth(char *p_mbrola)
{//============================
// p_mbrola is a string of mbrola pho lines
	int len;
	int finished;
	int result=0;

	if(synth_callback == NULL)
		return(1);

	if(p_mbrola == NULL)
		flush_MBR();
	else
		result = write_MBR(p_mbrola);


	finished = 0;
	while(!finished && ((len = read_MBR((short *)outbuf, outbuf_size/2)) > 0))
	{
		out_ptr = outbuf + len*2;

		if(event_list)
			event_list[event_list_ix].type = espeakEVENT_LIST_TERMINATED; // indicates end of event list
		count_samples += len;
		finished = synth_callback((short *)outbuf, len, event_list);
		event_list_ix=0;
	}

	if(finished)
	{
		// cancelled by user, discard any unused mbrola speech
		flush_MBR();
		while((len = read_MBR((short *)outbuf, outbuf_size/2)) > 0);
	}
	return(finished);
}  // end of SynthMbrola
#endif



void MbrolaTranslate(PHONEME_LIST *plist, int n_phonemes, FILE *f_mbrola)
{//======================================================================
// Generate a mbrola pho file
	int name;
	int phix;
	int len;
	int len1;
	PHONEME_TAB *ph;
	PHONEME_TAB *ph_next;
	PHONEME_TAB *ph_prev;
	PHONEME_LIST *p;
	PHONEME_LIST *next;
	PHONEME_LIST *prev;
	int pause = 0;
	int released;
	int name2;
	int control;
	int done;
	int len_percent;
	char buf[80];
	char mbr_buf[120];

#ifdef USE_MBROLA_DLL
	int embedded_ix=0;
	int word_count=0;

	event_list_ix = 0;
	out_ptr = outbuf;
	setNoError_MBR(1);     // don't stop on phoneme errors
#else
//	fprintf(f_mbrola,";; v=%.2f\n",(float)(mbrola_control & 0xff)/16.0);   //  ;; v=  has no effect on mbrola
#endif
	for(phix=1; phix < n_phonemes; phix++)
	{
		mbr_buf[0] = 0;

		p = &plist[phix];
		next = &plist[phix+1];
		prev = &plist[phix-1];
		ph = p->ph;
		ph_prev = plist[phix-1].ph;
		ph_next = plist[phix+1].ph;

#ifdef USE_MBROLA_DLL
		if(p->synthflags & SFLAG_EMBEDDED)
		{
			MbrolaEmbedded(embedded_ix, p->sourceix);
		}
		if(p->newword & 1)
			MbrolaMarker(espeakEVENT_WORD, (p->sourceix & 0x7ff) + clause_start_char, p->sourceix >> 11, clause_start_word + word_count++);
#endif

		name = GetMbrName(p,ph,ph_prev,ph_next,&name2,&len_percent,&control);
		if(control & 1)
			phix++;

		if(name == 0)
			continue;   // ignore this phoneme

		if(ph->type == phPAUSE)
		{
			name = '_';
			len = (p->length * speed_factor1)/256;
			if(len == 0) continue;
		}
		else
			len = (70 * speed_factor2)/256;

		sprintf(buf,"%s\t",WordToString(name));
		strcat(mbr_buf,buf);

		if(name2 == '_')
		{
			// add a pause after this phoneme
			pause = PauseLength(len_percent);
			name2 = 0;
		}

		done = 0;

		switch(ph->type)
		{
		case phVOWEL:
			len = ph->std_length;
			if(p->synthflags & SFLAG_LENGTHEN)
				len += phoneme_tab[phonLENGTHEN]->std_length;  // phoneme was followed by an extra : symbol

			if(ph_next->type == phPAUSE)
				len += 50;        // lengthen vowels before a pause
			len = (len * p->length)/256;

			if(name2 == 0)
			{
				sprintf(buf,"%d\t%s", len, WritePitch(p->env,p->pitch1,p->pitch2,0));
				strcat(mbr_buf,buf);
			}
			else
			{
				len1 = (len * len_percent)/100;
				sprintf(buf,"%d\t%s", len1, WritePitch(p->env,p->pitch1,p->pitch2,len_percent));
				strcat(mbr_buf,buf);

				sprintf(buf,"%s\t%d\t%s", WordToString(name2), len-len1, WritePitch(p->env,p->pitch1,p->pitch2,-len_percent));
				strcat(mbr_buf,buf);
			}
			done = 1;
			break;

		case phSTOP:
			released = 0;
			if(next->type==phVOWEL) released = 1;
			if(next->type==phLIQUID && !next->newword) released = 1;

			if(released)
				len = DoSample(p->ph,next->ph,2,0,-1);
			else
				len = DoSample(p->ph,phoneme_tab[phonPAUSE],2,0,-1);
			len = (len * 1000)/samplerate;  // convert to mS
			len += PauseLength(p->prepause);
			break;

		case phVSTOP:
			len = (80 * speed_factor2)/256;
			break;

		case phFRICATIVE:
			len = 0;
			if(p->synthflags & SFLAG_LENGTHEN)
				len = DoSample(ph,ph_next,2,p->length,-1);  // play it twice for [s:] etc.
			len += DoSample(ph,ph_next,2,p->length,-1);

			len = (len * 1000)/samplerate;  // convert to mS
			break;

		case phNASAL:
			if(next->type != phVOWEL)
			{
				len = DoSpect(p->ph,prev->ph,phoneme_tab[phonPAUSE],2,p,-1);
				len = (len * 1000)/samplerate;
			}
			break;
		}

		if(!done)
		{
			if(name2 != 0)
			{
				len1 = (len * len_percent)/100;
				sprintf(buf,"%d\n%s\t",len1,WordToString(name2));
				strcat(mbr_buf,buf);
				len -= len1;
			}
			sprintf(buf,"%d\n",len);
			strcat(mbr_buf,buf);
		}

		if(pause)
		{
			sprintf(buf,"_ \t%d\n",PauseLength(pause));
			strcat(mbr_buf,buf);
			pause = 0;
		}

		if(f_mbrola)
		{
			fwrite(mbr_buf,1,strlen(mbr_buf),f_mbrola);  // write .pho to a file
		}
		else
		{
#ifdef USE_MBROLA_DLL
			if(MbrolaSynth(mbr_buf) != 0)
				return;
#endif
		}
	}

#ifdef USE_MBROLA_DLL
	MbrolaSynth(NULL);
#endif
}  // end of SynthMbrola

