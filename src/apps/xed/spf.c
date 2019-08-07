/* sdl_picofont

   http://nurd.se/~noname/sdl_picofont

   File authors:
      Fredrik Hultin

   License: GPLv2
*/

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#include "SDL_picofont.h"

#include <string.h>

__attribute__((always_inline))
static inline unsigned int round(double d)
{ return (unsigned int)(d + 0.5); }


FNT_xy FNT_Generate(const char* text, unsigned int len, unsigned int w, unsigned char* pixels)
{
	unsigned int i, x, y, col, row, stop;
	unsigned char *fnt, chr;
	FNT_xy xy;

	fnt = FNT_GetFont();

	xy.x = xy.y = col = row = stop = 0;

	for(i = 0; i < len; i++){
		switch(text[i]){
			case '\n':
				row++;
				col = 0;
				chr = 0;
				break;

			case '\r':
				chr = 0;
				break;

			case '\t':
				chr = 0;
				col += 4 - col % 4;
				break;
		
			case '\0':
				stop = 1;
				chr = 0;
				break;
	
			default:
				col++;
				chr = text[i];
				break;
		}

		if(stop){
			break;
		}

		if((col + 1) * FNT_FONTWIDTH > (unsigned int)xy.x){
			xy.x = col * FNT_FONTWIDTH;
		}
		
		if((row + 1) * FNT_FONTHEIGHT * FNT_FONTVSCALE > (unsigned int)xy.y){
			xy.y = (row + 1) * (FNT_FONTHEIGHT * FNT_FONTVSCALE + FNT_FONTVPADDING);
		}

		if(chr != 0 && w != 0){
			for(y = 0; y < FNT_FONTHEIGHT * FNT_FONTVSCALE; y++){
				for(x = 0; x < FNT_FONTWIDTH; x++){
					if(fnt[text[i] * FNT_FONTHEIGHT + round(y / FNT_FONTVSCALE)] >> (7 - x) & 1){
						pixels[((col - 1) * FNT_FONTWIDTH) + x + (y + row * (round(FNT_FONTHEIGHT * FNT_FONTVSCALE) + FNT_FONTVPADDING)) * w] = 1;
					}
				}
			}
		}
	}

	return xy;	
}

#ifdef __cplusplus
};
#endif /* __cplusplus */
