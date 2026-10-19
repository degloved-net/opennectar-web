// nectar_catalog <dataDir> [carpeta de caché]
// Construye (o pone al día) el catálogo de assets del editor de mapas y
// resume lo que hay por categoría.

#include "nectar_assets/catalog.h"

#include <cstdio>
#include <map>

int main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "uso: %s <dataDir> [carpeta de caché]\n", argv[0]);
		return 2;
	}
	const std::string cache = argc > 2 ? argv[2] : nectar::defaultCacheDir();
	nectar::Catalog cat;
	std::string error;
	const bool ok = nectar::buildCatalog(argv[1], cache, cat, error, [](std::size_t done, std::size_t total, const std::string&) {
		if (done % 100 == 0 || done == total) fprintf(stderr, "\r%zu / %zu", done, total);
	});
	fprintf(stderr, "\n");
	if (!ok) {
		fprintf(stderr, "error: %s\n", error.c_str());
		return 1;
	}

	std::map<std::string, int> perCategory;
	int errors = 0, thumbs = 0;
	for (const nectar::CatalogEntry& e : cat.entries) {
		perCategory[e.category]++;
		if (!e.thumb.empty()) thumbs++;
		if (!e.error.empty()) {
			errors++;
			printf("  ! %s: %s\n", e.path.c_str(), e.error.c_str());
		}
	}
	printf("%zu assets (%d reutilizados, %d leídos), %d miniaturas, %d con error\n", cat.entries.size(), cat.reused, cat.rebuilt, thumbs,
	       errors);
	for (const auto& c : perCategory) printf("  %-12s %d\n", c.first.c_str(), c.second);
	printf("caché: %s\n", cache.c_str());
	return 0;
}
