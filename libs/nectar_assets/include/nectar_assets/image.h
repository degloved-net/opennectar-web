#pragma once

// Imágenes RGBA8 y texturas de GameCube (.bti y las que van dentro de los .mod).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

struct Image {
	int width  = 0;
	int height = 0;
	std::vector<std::uint8_t> rgba; // fila a fila, 4 bytes por píxel

	bool empty() const { return width <= 0 || height <= 0; }
	const std::uint8_t* at(int x, int y) const { return &rgba[(std::size_t(y) * width + x) * 4]; }
	std::uint8_t* at(int x, int y) { return &rgba[(std::size_t(y) * width + x) * 4]; }
	void resize(int w, int h)
	{
		width  = w;
		height = h;
		rgba.assign(std::size_t(w) * h * 4, 0);
	}
};

/// Formatos de textura de un .mod (TexImgFormat del juego).
enum ModTexFormat {
	MOD_TEX_RGB565 = 0,
	MOD_TEX_CMPR   = 1,
	MOD_TEX_RGB5A3 = 2,
	MOD_TEX_I4     = 3,
	MOD_TEX_I8     = 4,
	MOD_TEX_IA4    = 5,
	MOD_TEX_IA8    = 6,
	MOD_TEX_RGBA8  = 7,
	MOD_TEX_Z8     = 8,
};

/// Archivo .bti completo (cabecera + datos). Las paletizadas no.
bool decodeBti(const std::uint8_t* data, std::size_t size, Image& out);
bool loadBti(const std::string& path, Image& out);

/// Imagen base (sin mipmaps) de una textura de .mod.
bool decodeModTexture(int format, int width, int height, const std::uint8_t* data, std::size_t size, Image& out);

/// Reduce (media por cajas) para que quepa en maxW x maxH; nunca amplía.
Image shrinkToFit(const Image& src, int maxW, int maxH);

bool writePng(const std::string& path, const Image& img);

} // namespace nectar
