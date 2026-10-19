#pragma once

// Compilación del mapa en segundo plano con el mismo compilador que el juego
// (nectar_mapcore). Dos resultados por petición:
//  - "base": el mapa sin objetos (suelo, muros, charcas) para la vista; los
//    objetos se dibujan aparte para que se muevan sin esperar;
//  - "full": el mapa entero, para la colisión real, los caminos y los errores.

#include "nectar_assets/ini.h"
#include "nectar_assets/mod.h"
#include "nectar_mapcore/map.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nm {

struct PreviewResult {
	int generation = -1;
	bool ok        = false; ///< hay geometría (aunque el mapa aún no se pueda jugar)
	bool playable  = false; ///< además, el juego lo aceptaría
	std::string error;
	nectar::Model base, full;
	nectar::Routes routes;
	std::size_t vertices = 0, collisionTris = 0, routePoints = 0, props = 0, bytes = 0;
};

class Preview {
public:
	~Preview();
	void start(const std::string& dataDir);
	/// Pide compilar este documento (la última petición sustituye a las demás).
	void request(const nectar::MapDoc& doc, int generation);
	/// Si hay un resultado nuevo lo deja en `out` y devuelve true.
	bool poll(PreviewResult& out);
	bool busy() const;

	/// Compila en este hilo (para la miniatura al guardar).
	static bool compileNow(const std::string& dataDir, const nectar::MapDoc& doc, PreviewResult& out);

private:
	void run();
	std::string dataDir_;
	std::thread thread_;
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	bool quit_ = false, hasRequest_ = false, hasResult_ = false, working_ = false;
	nectar::MapDoc request_;
	int requestGen_ = -1;
	PreviewResult result_;
	std::map<std::string, std::vector<std::uint8_t>> files_; ///< del hilo de trabajo
};

} // namespace nm
