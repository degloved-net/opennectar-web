#include "preview.h"

#include "nectar_mapcore/compile.h"

#include <fstream>
#include <iterator>

#include "nectar_mapcore/i18n.h"

namespace nm {

using nectar::tr;

namespace {

bool readFile(const std::string& dataDir, std::map<std::string, std::vector<std::uint8_t>>* cache, const std::string& path,
              std::vector<std::uint8_t>& out)
{
	if (path.find("..") != std::string::npos) return false;
	if (cache) {
		auto it = cache->find(path);
		if (it != cache->end()) {
			out = it->second;
			return true;
		}
	}
	std::ifstream f(dataDir + "/" + path, std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	if (cache) (*cache)[path] = out;
	return true;
}

bool compileDoc(const std::string& dataDir, std::map<std::string, std::vector<std::uint8_t>>* cache, const nectar::MapDoc& doc,
                PreviewResult& out)
{
	out.ok       = false;
	out.playable = false;
	out.error.clear();
	// La geometría sale aunque el mapa no esté terminado (algo sin suelo,
	// faltan piezas...): así la vista nunca se queda vacía.
	nectar::MapData data;
	if (!nectar::expandMap(doc, data, out.error, false)) return false;
	{
		nectar::MapData checked;
		std::string why;
		out.playable = nectar::expandMap(doc, checked, why);
		if (!out.playable) out.error = why;
	}
	auto read = [&](const std::string& p, std::vector<std::uint8_t>& o) { return readFile(dataDir, cache, p, o); };

	nectar::CompiledMap full;
	if (!nectar::compileMap(data, read, "courses/vsarena/arena.mod", full, out.error)) return false;
	std::string err;
	if (!nectar::parseMod(full.mod.data(), full.mod.size(), out.full, err)) {
		out.error = tr("el mapa compilado no se puede leer: ") + err;
		return false;
	}
	nectar::parseRoutes(out.full.trailingIni, out.routes, err);
	out.vertices      = full.vertices;
	out.collisionTris = full.collisionTris;
	out.routePoints   = full.routePoints;
	out.props         = full.props;
	out.bytes         = full.mod.size();

	if (data.props.empty()) {
		out.base = out.full;
	} else {
		data.props.clear();
		nectar::CompiledMap base;
		if (!nectar::compileMap(data, read, "courses/vsarena/arena.mod", base, out.error)) return false;
		if (!nectar::parseMod(base.mod.data(), base.mod.size(), out.base, err)) {
			out.error = tr("el mapa compilado no se puede leer: ") + err;
			return false;
		}
	}
	out.ok = true;
	return out.playable;
}

} // namespace

Preview::~Preview()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		quit_ = true;
	}
	cv_.notify_all();
	if (thread_.joinable()) thread_.join();
}

void Preview::start(const std::string& dataDir)
{
	dataDir_ = dataDir;
	if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
}

void Preview::request(const nectar::MapDoc& doc, int generation)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		request_    = doc;
		requestGen_ = generation;
		hasRequest_ = true;
	}
	cv_.notify_all();
}

bool Preview::poll(PreviewResult& out)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!hasResult_) return false;
	out        = std::move(result_);
	hasResult_ = false;
	return true;
}

bool Preview::busy() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return working_ || hasRequest_;
}

void Preview::run()
{
	for (;;) {
		nectar::MapDoc doc;
		int gen = -1;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait(lock, [&] { return quit_ || hasRequest_; });
			if (quit_) return;
			doc         = request_;
			gen         = requestGen_;
			hasRequest_ = false;
			working_    = true;
		}
		PreviewResult r;
		r.generation = gen;
		compileDoc(dataDir_, &files_, doc, r);
		{
			std::lock_guard<std::mutex> lock(mutex_);
			result_    = std::move(r);
			hasResult_ = true;
			working_   = false;
		}
	}
}

bool Preview::compileNow(const std::string& dataDir, const nectar::MapDoc& doc, PreviewResult& out)
{
	return compileDoc(dataDir, nullptr, doc, out);
}

} // namespace nm
