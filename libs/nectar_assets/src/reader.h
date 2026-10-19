#pragma once

// Lectura big-endian con límites, para los formatos del juego. Un fallo deja
// `ok` en false y las lecturas siguientes devuelven 0: se comprueba al final.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace nectar {

struct Reader {
	const std::uint8_t* data = nullptr;
	std::size_t size         = 0;
	std::size_t pos          = 0;
	bool ok                  = true;

	Reader(const std::uint8_t* d, std::size_t n)
	    : data(d)
	    , size(n)
	{
	}

	std::size_t left() const { return pos < size ? size - pos : 0; }
	bool need(std::size_t n)
	{
		if (!ok || n > left()) {
			ok = false;
			return false;
		}
		return true;
	}
	void seek(std::size_t p)
	{
		if (p > size) ok = false;
		else pos = p;
	}
	void skip(std::size_t n)
	{
		if (need(n)) pos += n;
	}
	/// Alinea la posición absoluta (skipPadding del juego).
	void align(std::size_t a) { seek((pos + a - 1) / a * a); }

	std::uint8_t u8()
	{
		if (!need(1)) return 0;
		return data[pos++];
	}
	std::uint16_t u16()
	{
		if (!need(2)) return 0;
		const std::uint16_t v = std::uint16_t((data[pos] << 8) | data[pos + 1]);
		pos += 2;
		return v;
	}
	std::int16_t s16() { return std::int16_t(u16()); }
	std::uint32_t u32()
	{
		if (!need(4)) return 0;
		const std::uint32_t v = (std::uint32_t(data[pos]) << 24) | (std::uint32_t(data[pos + 1]) << 16) | (std::uint32_t(data[pos + 2]) << 8)
		                      | data[pos + 3];
		pos += 4;
		return v;
	}
	std::int32_t s32() { return std::int32_t(u32()); }
	float f32()
	{
		const std::uint32_t v = u32();
		float f;
		std::memcpy(&f, &v, 4);
		return f;
	}
	/// Identificador de 4 letras de los .gen: guardado al revés ("1.0v" = 'v0.1').
	std::uint32_t id()
	{
		if (!need(4)) return 0;
		const std::uint32_t v = (std::uint32_t(data[pos + 3]) << 24) | (std::uint32_t(data[pos + 2]) << 16)
		                      | (std::uint32_t(data[pos + 1]) << 8) | data[pos];
		pos += 4;
		return v;
	}
	/// Cadena con su longitud delante (Stream::readString).
	std::string str()
	{
		const std::int32_t n = s32();
		if (n < 0 || !need(std::size_t(n))) {
			ok = false;
			return std::string();
		}
		std::string s(reinterpret_cast<const char*>(data + pos), std::size_t(n));
		pos += std::size_t(n);
		const std::size_t nul = s.find('\0');
		return nul == std::string::npos ? s : s.substr(0, nul);
	}
};

/// 'teki' -> "teki"; con letras no imprimibles, en hexadecimal.
inline std::string idString(std::uint32_t id)
{
	char s[5] = { char(id >> 24), char(id >> 16), char(id >> 8), char(id), 0 };
	for (int i = 0; i < 4; i++) {
		if (s[i] < 0x20 || s[i] > 0x7e) {
			char hex[11];
			snprintf(hex, sizeof(hex), "0x%08x", id);
			return hex;
		}
	}
	return s;
}

} // namespace nectar
