#pragma once

// Índice de los assets de un dataDir para la barra lateral del editor:
// categoría, datos básicos y una miniatura de cada uno. Se guarda en una
// caché (index.json + thumbs/) y solo se rehace lo que ha cambiado.

#include "nectar_assets/mod.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nectar {

enum class AssetKind { Model, Texture, Generators, Scene };

struct CatalogEntry {
	std::string path; ///< relativa a dataDir, con '/'
	AssetKind kind = AssetKind::Model;
	std::string category; ///< terrain, objects, plants, ship_parts, pellets, enemies, pikmin, effects, ui, textures, stages, other
	std::uint64_t fileSize = 0;
	std::int64_t mtime     = 0;

	// Modelos
	int triangles = 0, vertices = 0, textures = 0, joints = 0, collisionTris = 0;
	Vec3 boundsMin, boundsMax;
	// Texturas
	int width = 0, height = 0;
	// .gen
	int generators = 0;
	// Escenas (.ini)
	std::string mapFile;

	std::string thumb; ///< PNG en la caché; vacío si no tiene
	std::string error; ///< por qué no se pudo leer
};

struct Catalog {
	std::string dataDir, cacheDir;
	std::vector<CatalogEntry> entries;
	int reused = 0, rebuilt = 0; ///< de la última vez que se construyó
};

/// ~/.cache/nectar_mapmaker (o $XDG_CACHE_HOME; %LOCALAPPDATA% en Windows).
std::string defaultCacheDir();

/// Categoría de un asset por su ruta.
std::string assetCategory(const std::string& relPath, AssetKind kind);

using CatalogProgress = std::function<void(std::size_t done, std::size_t total, const std::string& path)>;

/// Recorre dataDir y deja el índice al día en cacheDir. false solo si
/// dataDir no existe o la caché no se puede escribir; los assets que no se
/// leen quedan con `error`.
bool buildCatalog(const std::string& dataDir, const std::string& cacheDir, Catalog& out, std::string& error,
                  const CatalogProgress& progress = CatalogProgress());

/// Lee el índice guardado sin recorrer nada.
bool loadCatalog(const std::string& cacheDir, Catalog& out, std::string& error);

} // namespace nectar
