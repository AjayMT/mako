/* sdl_picofont

   http://nurd.se/~noname/sdl_picofont

   File authors:
      Fredrik Hultin

   License: GPLv2
*/

#ifndef SDL_PICOFONT_H
#define SDL_PICOFONT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define FNT_FONTHEIGHT 8
#define FNT_FONTWIDTH 8

typedef struct
{
	int x;
	int y;
}FNT_xy;

unsigned char* FNT_GetFont();
FNT_xy FNT_Generate(const char* text, unsigned int len, unsigned int w, unsigned char* pixels);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* SDL_PICOFONT_H */
