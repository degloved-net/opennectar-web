#pragma once

// Idioma de los textos del editor y de los mensajes del mapa. Los textos se
// escriben en español en el código y tr() devuelve su traducción (inglés por
// defecto); si falta una traducción, sale el español.

#include <string>

namespace nectar {

enum class Language { English, Spanish };

void setLanguage(Language lang);
Language language();
/// "en" / "es" (cualquier otra cosa: inglés).
Language languageFromCode(const std::string& code);
const char* languageCode(Language lang);

/// Traducción de un texto escrito en español (puntero estable).
const char* tr(const char* es);

} // namespace nectar
