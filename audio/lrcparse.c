/*
	VitaShell
	Copyright (C) 2015-2018, TheFloW

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "lrcparse.h"

static char* getStringFromRegmatch(char* source,size_t so,size_t eo)
{
    size_t wordlength = eo - so;
    char* word;

    if(wordlength < 2){
        word = malloc(sizeof(char));
        if (!word) return NULL;
        word[0] = '\0';
    }else{
        word = malloc(sizeof(char) * (wordlength + 1));
        if (!word) return NULL;
        memcpy((void*)word,(void*)(source + so),wordlength);//copy string
        word[sizeof(char) * (wordlength)] = '\0';
    }
    return word;
}

Lyrics* lrcParseLoadWithFile(char* lrcfilepath)
{
    int filesize = getFileSize(lrcfilepath);

    if (filesize < 0 || filesize > 64 * 1024) return NULL;
    char *lrcbuffer = calloc(1, filesize + 1);
    if (!lrcbuffer) return NULL;
    Lyrics *lyrics = NULL;
    if (ReadFile(lrcfilepath, lrcbuffer, filesize) == filesize)
        lyrics = lrcParseLoadWithBuffer(lrcbuffer);
    free(lrcbuffer);
    return lyrics;
}

Lyrics* lrcParseLoadWithBuffer(char* buffer)
{
    regmatch_t pm[5];
    regex_t preg;

    char* pattern = "\\[([0-9]{2}):([0-5][0-9])\\.([0-9]{2})\\](.*)";

    if (regcomp(&preg, pattern, REG_EXTENDED |REG_NEWLINE) != 0)
        return NULL;

    uint32_t lines = 0;//lyrics line count

    Lyricsline* lrcline = malloc(sizeof(Lyricsline) * MAX_LYRICSLINE);

    if (!lrcline) { regfree(&preg); return NULL; }
    while (lines < MAX_LYRICSLINE) {
        if(regexec(&preg,buffer, 5, pm, REG_NOTEOL) != REG_NOMATCH){

            char* m = getStringFromRegmatch(buffer,pm[1].rm_so,pm[1].rm_eo);
            char* s = getStringFromRegmatch(buffer,pm[2].rm_so,pm[2].rm_eo);
            char* ms = getStringFromRegmatch(buffer,pm[3].rm_so,pm[3].rm_eo);
            char* word = getStringFromRegmatch(buffer,pm[4].rm_so,pm[4].rm_eo);


            if (!m || !s || !ms || !word) {
                free(m); free(s); free(ms); free(word);
                goto failed;
            }
            lrcline[lines].m = atol(m);
            lrcline[lines].s = atoi(s);
            lrcline[lines].ms = atoi(ms);
            lrcline[lines].word = word;
            lrcline[lines].totalms = (atol(m) * 60 + atoi(s)) * 1000 + atoi(ms) * 10;

            free(m);
            free(s);
            free(ms);

            lines++;
            buffer+=pm[0].rm_eo;
        }else{break;}
    }

    Lyrics* lyrics = malloc(sizeof(Lyrics));
    if (!lyrics) goto failed;
    regfree(&preg);
    lyrics->lrclines = lrcline;
    lyrics->lyricscount = lines;

    return lyrics;
failed:
    regfree(&preg);
    for (uint32_t i = 0; i < lines; ++i) free(lrcline[i].word);
    free(lrcline);
    return NULL;
}

void lrcParseClose(Lyrics* lyrics)
{
    if(!lyrics)
        return;

    int i;
    for(i = 0;i < lyrics->lyricscount ; ++i){
        if(lyrics->lrclines[i].word){
            free(lyrics->lrclines[i].word);//free lyrics words
            lyrics->lrclines[i].word = NULL;
        }
    }

    if(lyrics->lrclines){
        free(lyrics->lrclines);//free lrclines struct
        lyrics->lrclines = NULL;
    }

    free(lyrics);//free lyrics
    lyrics = NULL;
}
