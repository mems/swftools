/* png2swf.c

   PNG to SWF converter tool

   Part of the swftools package.

   Copyright (c) 2002,2003 Matthias Kramm <kramm@quiss.org>
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
 
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <zlib.h>
#include "../lib/rfxswf.h"
#include "../lib/args.h"

#define MAX_INPUT_FILES 1024
#define VERBOSE(x) (global.verbose>=x)

struct {
    float framerate;
    int max_image_width;
    int max_image_height;
    int force_width;
    int force_height;
    int nfiles;
    int verbose;
    int do_cgi;
    int version;
    char *outfile;
} global;

struct {
    char *filename;
} image[MAX_INPUT_FILES];

TAG *MovieStart(SWF * swf, float framerate, int dx, int dy)
{
    TAG *t;
    RGBA rgb;

    memset(swf, 0x00, sizeof(SWF));

    swf->fileVersion = global.version;
    swf->frameRate = (int)(256.0 * framerate);
    swf->movieSize.xmax = dx * 20;
    swf->movieSize.ymax = dy * 20;

    t = swf->firstTag = swf_InsertTag(NULL, ST_SETBACKGROUNDCOLOR);

    rgb.r = rgb.g = rgb.b = rgb.a = 0x00;
    //rgb.g = 0xff; //<--- handy for testing alpha conversion
    swf_SetRGB(t, &rgb);

    return t;
}

int MovieFinish(SWF * swf, TAG * t, char *sname)
{
    int f, so = fileno(stdout);
    t = swf_InsertTag(t, ST_END);

    if ((!isatty(so)) && (!sname))
	f = so;
    else {
	if (!sname)
	    sname = "output.swf";
	f = open(sname,O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, 0644);
    }

    if(global.do_cgi) {
	if FAILED(swf_WriteCGI(swf)) fprintf(stderr,"WriteCGI() failed.\n");
    } else {
	if(global.version >= 6) {
	    if (swf_WriteSWC(f, swf)<0) 
		    fprintf(stderr, "Unable to write output file: %s\n", sname);
	} else {
	    if (swf_WriteSWF(f, swf)<0) 
		    fprintf(stderr, "Unable to write output file: %s\n", sname);
	}
	if (f != so)
	    close(f);
    }

    swf_FreeTags(swf);
    return 0;
}

int png_read_chunk(char (*head)[4], int*destlen, U8**destdata, FILE*fi)
{
    unsigned int len;
    if(destlen) *destlen=0;
    if(destdata) *destdata=0;
    if(!fread(&len, 4, 1, fi))
	return 0;
    if(!fread(head, 4, 1, fi))
	return 0;
    len = REVERSESWAP32(len);
    if(destlen) *destlen = len;
    if(destdata) {
	if(len)
	    *destdata = malloc(len);
	else 
	    *destdata = 0;
	if(!fread(*destdata, len, 1, fi)) {
	    *destdata = 0;
	    if(destlen) *destlen=0;
	    return 0;
	}
	fseek(fi, 4, SEEK_CUR);

    } else {
	fseek(fi, len+4, SEEK_CUR);
    }
    return 1;
}

unsigned int png_get_dword(FILE*fi)
{
    unsigned int a;
    fread(&a,4,1,fi);
    return REVERSESWAP32(a);
}

struct png_header
{
    int width;
    int height;
    int bpp;
    int mode;
};

int png_read_header(FILE*fi, struct png_header*header)
{
    char id[4];
    int len;
    int ok=0;
    U8 head[8] = {137,80,78,71,13,10,26,10};
    U8 head2[8];
    U8*data;
    fread(head2,8,1,fi);
    if(strncmp(head,head2,4))
	return 0;
   
    while(png_read_chunk(&id, &len, &data, fi))
    {
	if(VERBOSE(2))
	    printf("%c%c%c%c %d\n", id[0],id[1],id[2],id[3],len);
	if(!strncasecmp(id, "IHDR", 4)) {
	    char a,b,c,f,i;
	    if(len < 8) exit(1);
	    header->width = REVERSESWAP32(*(U32*)&data[0]);
	    header->height = REVERSESWAP32(*(U32*)&data[4]);
	    a = data[8];      // should be 8
	    b = data[9];      // should be 3(indexed) or 2(rgb)

	    c = data[10];     // compression mode (0)
	    f = data[11];     // filter mode (0)
	    i = data[12];     // interlace mode (0)
	
	    if(VERBOSE(2)) printf("image mode:%d\n", b);
	    if(VERBOSE(2)) printf("bpp: %d\n", a);
	    if(VERBOSE(2)) printf("compression: %d\n", c);
	    if(VERBOSE(2)) printf("filter: %d\n", f);
	    if(VERBOSE(2)) printf("interlace: %d\n", i);

	    if(b!=0 && b!=2 && b!=3 && b!=6) {
		fprintf(stderr, "Image mode %d not supported!\n", b);
		if(b == 4) {
		    fprintf(stderr, "(This is a grayscale image with alpha channel-\n");
		    fprintf(stderr, " try converting it into an RGB image with alpha channel)\n");
		}
		exit(1);
	    }
	    if(a!=8 && (b==2 || b==6)) {
		fprintf(stderr, "Bpp %d in mode %d not supported!\n", a);
		exit(1);
	    }
	    if(c!=0) {
		fprintf(stderr, "Compression mode %d not supported!\n", c);
		exit(1);
	    }
	    if(f!=0) {
		fprintf(stderr, "Filter mode %d not supported!\n", f);
		exit(1);
	    }
	    if(i!=0) {
		fprintf(stderr, "Interlace mode %d not supported!\n", i);
		exit(1);
	    }
	    if(VERBOSE(2))
		printf("%dx%d %d %d %d %d %d\n",header->width, header->height, a,b,c,f,i);
	    header->bpp = a;
	    header->mode = b;
	    ok = 1;
	} 
	
	free(data);
    }
    return ok;
}

typedef unsigned char byte;
#define ABS(a) ((a)>0?(a):(-(a)))
byte inline PaethPredictor (byte a,byte b,byte c)
{
        // a = left, b = above, c = upper left
        int p = a + b - c;        // initial estimate
        int pa = ABS(p - a);      // distances to a, b, c
        int pb = ABS(p - b);
        int pc = ABS(p - c);
        // return nearest of a,b,c,
        // breaking ties in order a,b,c.
        if (pa <= pb && pa <= pc) 
		return a;
        else if (pb <= pc) 
		return b;
        else return c;
}

void applyfilter3(int mode, U8*src, U8*old, U8*dest, int width)
{
    int x;
    unsigned char lastr=0;
    unsigned char lastg=0;
    unsigned char lastb=0;
    unsigned char upperlastr=0;
    unsigned char upperlastg=0;
    unsigned char upperlastb=0;

    if(mode==0) {
	for(x=0;x<width;x++) {
	    dest[0] = 255;
	    dest[1] = src[0];
	    dest[2] = src[1];
	    dest[3] = src[2];
	    dest+=4;
	    src+=3;
	}
    }
    else if(mode==1) {
	for(x=0;x<width;x++) {
	    dest[0] = 255;
	    dest[1] = src[0]+lastr;
	    dest[2] = src[1]+lastg;
	    dest[3] = src[2]+lastb;
	    lastr = dest[1];
	    lastg = dest[2];
	    lastb = dest[3];
	    dest+=4;
	    src+=3;
	}
    }
    else if(mode==2) {
	for(x=0;x<width;x++) {
	    dest[0] = 255;
	    dest[1] = src[0]+old[1];
	    dest[2] = src[1]+old[2];
	    dest[3] = src[2]+old[3];
	    dest+=4;
	    old+=4;
	    src+=3;
	}
    }
    else if(mode==3) {
	for(x=0;x<width;x++) {
	    dest[0] = 255;
	    dest[1] = src[0]+(old[1]+lastr)/2;
	    dest[2] = src[1]+(old[2]+lastg)/2;
	    dest[3] = src[2]+(old[3]+lastb)/2;
	    lastr = dest[1];
	    lastg = dest[2];
	    lastb = dest[3];
	    dest+=4;
	    old+=4;
	    src+=3;
	}
    }
    else if(mode==4) {
	for(x=0;x<width;x++) {
	    dest[0] = 255;
	    dest[1] = src[0]+PaethPredictor(lastr,old[1],upperlastr);
	    dest[2] = src[1]+PaethPredictor(lastg,old[2],upperlastg);
	    dest[3] = src[2]+PaethPredictor(lastb,old[3],upperlastb);
	    lastr = dest[1];
	    lastg = dest[2];
	    lastb = dest[3];
	    upperlastr = old[1];
	    upperlastg = old[2];
	    upperlastb = old[3];
	    dest+=4;
	    old+=4;
	    src+=3;
	}
    }    

}

void applyfilter4(int mode, U8*src, U8*old, U8*dest, int width)
{
    int x;
    unsigned char lastr=0;
    unsigned char lastg=0;
    unsigned char lastb=0;
    unsigned char lasta=0; //TODO: 255?
    unsigned char upperlastr=0;
    unsigned char upperlastg=0;
    unsigned char upperlastb=0;
    unsigned char upperlasta=0; //TODO: 255?

    if(mode==0) {
	for(x=0;x<width;x++) {
	    dest[0] = src[3];
	    dest[1] = src[0];
	    dest[2] = src[1];
	    dest[3] = src[2];
	    dest+=4;
	    src+=4;
	}
    }
    else if(mode==1) {
	for(x=0;x<width;x++) {
	    dest[0] = src[3]+lasta;
	    dest[1] = src[0]+lastr;
	    dest[2] = src[1]+lastg;
	    dest[3] = src[2]+lastb;
	    lasta = dest[0];
	    lastr = dest[1];
	    lastg = dest[2];
	    lastb = dest[3];
	    dest+=4;
	    src+=4;
	}
    }
    else if(mode==2) {
	for(x=0;x<width;x++) {
	    dest[0] = src[3]+old[0];
	    dest[1] = src[0]+old[1];
	    dest[2] = src[1]+old[2];
	    dest[3] = src[2]+old[3];
	    dest+=4;
	    old+=4;
	    src+=4;
	}
    }
    else if(mode==3) {
	for(x=0;x<width;x++) {
	    dest[0] = src[3]+(old[0]+lasta)/2;
	    dest[1] = src[0]+(old[1]+lastr)/2;
	    dest[2] = src[1]+(old[2]+lastg)/2;
	    dest[3] = src[2]+(old[3]+lastb)/2;
	    lasta = dest[0];
	    lastr = dest[1];
	    lastg = dest[2];
	    lastb = dest[3];
	    dest+=4;
	    old+=4;
	    src+=4;
	}
    }
    else if(mode==4) {
	for(x=0;x<width;x++) {
	    dest[0] = src[3]+PaethPredictor(lasta,old[0],upperlasta);
	    dest[1] = src[0]+PaethPredictor(lastr,old[1],upperlastr);
	    dest[2] = src[1]+PaethPredictor(lastg,old[2],upperlastg);
	    dest[3] = src[2]+PaethPredictor(lastb,old[3],upperlastb);
	    lasta = dest[0];
	    lastr = dest[1];
	    lastg = dest[2];
	    lastb = dest[3];
	    upperlasta = old[0];
	    upperlastr = old[1];
	    upperlastg = old[2];
	    upperlastb = old[3];
	    dest+=4;
	    old+=4;
	    src+=4;
	}
    }    

}

void applyfilter1(int mode, U8*src, U8*old, U8*dest, int width)
{
    int x;
    unsigned char last=0;
    unsigned char upperlast=0;

    if(mode==0) {
	for(x=0;x<width;x++) {
	    *dest = *src;
	    dest++;
	    src++;
	}
    }
    else if(mode==1) {
	for(x=0;x<width;x++) {
	    *dest = *src+last;
	    last = *dest;
	    dest++;
	    src++;
	}
    }
    else if(mode==2) {
	for(x=0;x<width;x++) {
	    *dest = *src+*old;
	    dest++;
	    old++;
	    src++;
	}
    }
    else if(mode==3) {
	for(x=0;x<width;x++) {
	    *dest = *src+(*old+last)/2;
	    last = *dest;
	    dest++;
	    old++;
	    src++;
	}
    }
    else if(mode==4) {
	for(x=0;x<width;x++) {
	    *dest = *src+PaethPredictor(last,*old,upperlast);
	    last = *dest;
	    upperlast = *old;
	    dest++;
	    old++;
	    src++;
	}
    }    

}

TAG *MovieAddFrame(SWF * swf, TAG * t, char *sname, int id)
{
    SHAPE *s;
    SRECT r;
    MATRIX m;
    int fs;

    char tagid[4];
    int len;
    U8*data;
    U8*imagedata;
    U8*zimagedata=0;
    unsigned long int imagedatalen;
    unsigned long int zimagedatalen=0;
    U8*palette = 0;
    int palettelen = 0;
    U8*alphapalette = 0;
    int alphapalettelen = 0;
    struct png_header header;
    int bypp;

    FILE *fi;
    U8 *scanline;

    if ((fi = fopen(sname, "rb")) == NULL) {
	if (VERBOSE(1))
	    fprintf(stderr, "Read access failed: %s\n", sname);
	return t;
    }

    if(!png_read_header(fi, &header))
	return 0;

    if(header.mode == 3 || header.mode == 0) bypp = 1;
    else
    if(header.mode == 2) bypp = 3;
    else
    if(header.mode == 6) bypp = 4;
    else
	return 0;
    imagedatalen = bypp * header.width * header.height + 65536;
    imagedata = malloc(imagedatalen);

    fseek(fi,8,SEEK_SET);
    while(!feof(fi))
    {
	if(!png_read_chunk(&tagid, &len, &data, fi))
	    break;
	if(!strncmp(tagid, "IEND", 4)) {
	    break;
	}
	if(!strncmp(tagid, "PLTE", 4)) {
	    palette = data;
	    palettelen = len/3;
	    data = 0; //don't free data
	    if(VERBOSE(2))
		printf("%d colors in palette\n", palettelen);
	}
	if(!strncmp(tagid, "tRNS", 4)) {
	    if(header.mode == 3) {
		alphapalette = data;
		alphapalettelen = len;
		data = 0; //don't free data
		if(VERBOSE(2))
		    printf("found %d alpha colors\n", alphapalettelen);
	    }
	}
	if(!strncmp(tagid, "IDAT", 4)) {
	    if(!zimagedata) {
		zimagedatalen = len;
		zimagedata = malloc(len);
		memcpy(zimagedata,data,len);
	    } else {
		zimagedata = realloc(zimagedata, zimagedatalen+len);
		memcpy(&zimagedata[zimagedatalen], data, len);
		zimagedatalen += len;
	    }
	}
	if(data)
	    free(data);
    }
    
    if(!zimagedata || uncompress(imagedata, &imagedatalen, zimagedata, zimagedatalen) != Z_OK) {
	fprintf(stderr, "Couldn't uncompress %s!\n", sname);
	if(zimagedata)
	    free(zimagedata);
	return 0;
    }
    free(zimagedata);

    if(alphapalette)
	t = swf_InsertTag(t, ST_DEFINEBITSLOSSLESS2);
    else
	t = swf_InsertTag(t, ST_DEFINEBITSLOSSLESS);

    swf_SetU16(t, id);		// id
    if(header.mode == 2 || header.mode == 6) {
	U8*data2 = malloc(header.width*header.height*4);
	int i,s=0;
	int x,y;
	int pos=0;
	int opaque=0;
	int transparent=0;
	int semitransparent=0;
	/* in case for mode 2, the following also performs 24->32 bit conversion */
	unsigned char* firstline = malloc(header.width*4);

	for(y=0;y<header.height;y++) {
	    int mode = imagedata[pos++]; //filter mode
	    U8*src;
	    U8*dest;
	    U8*old;
	    dest = &data2[(y*header.width)*4];

	    if(header.bpp == 8)
	    {
		/* one byte per pixel */
		src = &imagedata[pos];
		pos+=header.width*(header.mode==6?4:3);
	    } else {
		/* not implemented yet */
		exit(1);
	    }

	    if(!y) {
		old = firstline;
		memset(old, 0, header.width*4); //TODO: fill alpha with 255?
	    } else {
		old = &data2[(y-1)*header.width*4];
	    }
	    if(header.mode==6)
		applyfilter4(mode, src, old, dest, header.width);
	    else if(header.mode==2)
		applyfilter3(mode, src, old, dest, header.width);
	}
	free(firstline);

	/* the image is now compressed and stored in data. Now let's take
	   a look at the alpha values */
	if(header.mode == 6) {
	    for(y=0;y<header.height;y++) {
		U8*l = &data2[(y*header.width)*4];
		for(x=0;x<header.width;x++) {
		    U8 a = l[x*4];
		    U8 b = l[x*4+1];
		    U8 g = l[x*4+2];
		    U8 r = l[x*4+3];
		    if(a==255) transparent++;
		    else {
			if(a==0) opaque++;
			else semitransparent++;
			l[x*4+3]=(int)r*a/255;
			l[x*4+2]=(int)g*a/255;
			l[x*4+1]=(int)b*a/255;
		    }
		}
	    }
	    if(semitransparent || opaque) {
		t->id = ST_DEFINEBITSLOSSLESS2;
	    }
	}
	swf_SetLosslessBits(t, header.width, header.height, data2, BMF_32BIT);
	free(data2);
    } else if(header.mode == 0 || header.mode == 3) {
	RGBA*rgba;
	int swf_width = BYTES_PER_SCANLINE(header.width);
	U8*data2 = malloc(swf_width*header.height);
	U8*tmpline = malloc(header.width);
	int i,x,y;
	int pos=0;
	if(header.mode == 3) { // palette or grayscale?
	    rgba = (RGBA*)malloc(palettelen*sizeof(RGBA));
	    if(!palette) {
		fprintf(stderr, "Error: No palette found!\n");
		exit(1);
	    }
	    /* 24->32 bit conversion */
	    for(i=0;i<palettelen;i++) {
		rgba[i].r = palette[i*3+0];
		rgba[i].g = palette[i*3+1];
		rgba[i].b = palette[i*3+2];
		if(alphapalette && i<alphapalettelen) {
		    rgba[i].a = alphapalette[i];
		    rgba[i].r = ((int)rgba[i].r*rgba[i].a)/255;
		    rgba[i].g = ((int)rgba[i].g*rgba[i].a)/255;
		    rgba[i].b = ((int)rgba[i].b*rgba[i].a)/255;
		} else {
		    rgba[i].a = 255;
		}
	    }
	} else {
            int mult = (0x1ff>>header.bpp);
	    palettelen = 1<<header.bpp;
	    rgba = (RGBA*)malloc(palettelen*sizeof(RGBA));
	    for(i=0;i<palettelen;i++) {
		rgba[i].r = i*mult;
		rgba[i].g = i*mult;
		rgba[i].b = i*mult;
	    }
	}

	for(y=0;y<header.height;y++) {
	    int mode = imagedata[pos++]; //filter mode
	    U8*old;
	    U8*dest = &data2[y*swf_width];
	    U8*src;
	    src = &imagedata[pos];
	    if(header.bpp == 8) {
		pos+=header.width;
	    } else {
		int x,s=0;
		int bitpos = 0;
		U32 v = (1<<header.bpp)-1;
		for(x=0;x<header.width;x++) {
		    U32 r = src[s/8]<<8 | 
			    src[s/8+1];
		    int t;
		    tmpline[x] = (r>>(16-header.bpp-(s&7)))&v;
		    s+=header.bpp;
		}
		src = tmpline;
		pos+=(header.width*header.bpp+7)/8;
	    }

	    if(!y) {
		memset(data2,0,swf_width);
		old = &data2[y*swf_width];
	    } else {
		old = &data2[(y-1)*swf_width];
	    }
	    applyfilter1(mode, src, old, dest, header.width);
	}
	swf_SetLosslessBitsIndexed(t, header.width, header.height, data2, rgba, palettelen);
	free(tmpline);
	free(rgba);
	free(data2);
    }

    t = swf_InsertTag(t, ST_DEFINESHAPE3);

    swf_ShapeNew(&s);
    swf_GetMatrix(NULL, &m);
    m.sx = 20 * 0x10000;
    m.sy = 20 * 0x10000;
    m.tx = -10;
    m.ty = -10;
    fs = swf_ShapeAddBitmapFillStyle(s, &m, id, 1);

    swf_SetU16(t, id + 1);	// id

    r.xmin = r.ymin = 0;
    r.xmax = header.width * 20;
    r.ymax = header.height * 20;
    swf_SetRect(t, &r);

    swf_SetShapeHeader(t, s);

    swf_ShapeSetAll(t, s, 0, 0, 0, fs, 0);
    swf_ShapeSetLine(t, s, r.xmax, 0);
    swf_ShapeSetLine(t, s, 0, r.ymax);
    swf_ShapeSetLine(t, s, -r.xmax, 0);
    swf_ShapeSetLine(t, s, 0, -r.ymax);

    swf_ShapeSetEnd(t);

    t = swf_InsertTag(t, ST_REMOVEOBJECT2);
    swf_SetU16(t, 50);		// depth

    t = swf_InsertTag(t, ST_PLACEOBJECT2);

    swf_GetMatrix(NULL, &m);
    m.tx = (swf->movieSize.xmax - (int) header.width * 20) / 2;
    m.ty = (swf->movieSize.ymax - (int) header.height * 20) / 2;
    swf_ObjectPlace(t, id + 1, 50, &m, NULL, NULL);

    t = swf_InsertTag(t, ST_SHOWFRAME);

    fclose(fi);

    return t;
}


int CheckInputFile(char *fname, char **realname)
{
    FILE *fi;
    char *s = malloc(strlen(fname) + 5);
    struct png_header head;

    if (!s)
	exit(2);
    (*realname) = s;
    strcpy(s, fname);

    // Check whether file exists (with typical extensions)

    if ((fi = fopen(s, "rb")) == NULL) {
	sprintf(s, "%s.png", fname);
	if ((fi = fopen(s, "rb")) == NULL) {
	    sprintf(s, "%s.PNG", fname);
	    if ((fi = fopen(s, "rb")) == NULL) {
		sprintf(s, "%s.Png", fname);
		if ((fi = fopen(s, "rb")) == NULL) {
		    fprintf(stderr, "Couldn't open %s!\n", fname);
		    return -1;
		}
	    }
	}
    }

    if(!png_read_header(fi, &head)) {
	fprintf(stderr, "%s is not a PNG file!\n", fname);
	return -1;
    }

    if (global.max_image_width < head.width)
	global.max_image_width = head.width;
    if (global.max_image_height < head.height)
	global.max_image_height = head.height;

    fclose(fi);

    return 0;
}

int args_callback_option(char *arg, char *val)
{
    int res = 0;
    if (arg[1])
	res = -1;
    else
	switch (arg[0]) {
	case 'r':
	    if (val)
		global.framerate = atof(val);
	    /* removed framerate>0 restriction in order to make
	       Flash Communication Server compatible SWFs */
	    if ((global.framerate < 0) ||(global.framerate >= 256.0)) {
		if (VERBOSE(1))
		    fprintf(stderr,
			    "Error: You must specify a valid framerate between 1/256 and 255.\n");
		exit(1);
	    }
	    res = 1;
	    break;

	case 'o':
	    if (val)
		global.outfile = val;
	    res = 1;
	    break;

	case 'z':
	    global.version = 6;
	    res = 0;
	    break;

	case 'C':
	    global.do_cgi = 1;
	    break;

	case 'v':
	    if (val)
		global.verbose = atoi(val);
	    res = 1;
	    break;

	case 'X':
	    if (val)
		global.force_width = atoi(val);
	    res = 1;
	    break;

	case 'Y':
	    if (val)
		global.force_height = atoi(val);
	    res = 1;
	    break;

	case 'V':
	    printf("png2swf - part of %s %s\n", PACKAGE, VERSION);
	    exit(0);

	default:
	    res = -1;
	    break;
	}

    if (res < 0) {
	if (VERBOSE(1))
	    fprintf(stderr, "Unknown option: -%s\n", arg);
	exit(1);
	return 0;
    }
    return res;
}

static struct options_t options[] = {
{"r", "rate"},
{"o", "output"},
{"z", "zlib"},
{"X", "pixel"},
{"Y", "pixel"},
{"v", "verbose"},
{"C", "cgi"},
{"V", "version"},
{0,0}
};

int args_callback_longoption(char *name, char *val)
{
    return args_long2shortoption(options, name, val);
}

int args_callback_command(char *arg, char *next)	// actually used as filename
{
    char *s;
    if (CheckInputFile(arg, &s) < 0) {
	if (VERBOSE(1))
	    fprintf(stderr, "Error opening input file: %s\n", arg);
	free(s);
    } else {
	image[global.nfiles].filename = s;
	global.nfiles++;
	if (global.nfiles >= MAX_INPUT_FILES) {
	    if (VERBOSE(1))
		fprintf(stderr, "Error: Too many input files.\n");
	    exit(1);
	}
    }
    return 0;
}

void args_callback_usage(char *name)
{
    printf("\n");
    printf("Usage: %s [-X width] [-Y height] [-o file.swf] [-r rate] file1.png [file2.png...]\n", name);
    printf("\n");
    printf("-r , --rate <framerate>        Set movie framerate (frames per second)\n");
    printf("-o , --output <filename>       Set name for SWF output file.\n");
    printf("-z , --zlib <zlib>             Enable Flash 6 (MX) Zlib Compression\n");
    printf("-X , --pixel <width>           Force movie width to <width> (default: autodetect)\n");
    printf("-Y , --pixel <height>          Force movie height to <height> (default: autodetect)\n");
    printf("-v , --verbose <level>         Set verbose level (0=quiet, 1=default, 2=debug)\n");
    printf("-C , --cgi                     For use as CGI- prepend http header, write to stdout\n");
    printf("-V , --version                 Print version information and exit\n");
    printf("\n");
}

int main(int argc, char **argv)
{
    SWF swf;
    TAG *t;

    memset(&global, 0x00, sizeof(global));

    global.framerate = 1.0;
    global.verbose = 1;
    global.version = 4;

    processargs(argc, argv);
    
    if(global.nfiles<=0) {
	fprintf(stderr, "No png files found in arguments\n");
	return 1;
    }

    if (VERBOSE(2))
	fprintf(stderr, "Processing %i file(s)...\n", global.nfiles);

    t = MovieStart(&swf, global.framerate,
		   global.force_width ? global.force_width : global.
		   max_image_width,
		   global.force_height ? global.force_height : global.
		   max_image_height);

    {
	int i;
	for (i = 0; i < global.nfiles; i++) {
	    if (VERBOSE(3))
		fprintf(stderr, "[%03i] %s\n", i,
			image[i].filename);
	    t = MovieAddFrame(&swf, t, image[i].filename, (i * 2) + 1);
	    free(image[i].filename);
	}
    }

    MovieFinish(&swf, t, global.outfile);

    return 0;
}
