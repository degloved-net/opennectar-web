#pragma once

// Miniaturas de modelos sin GPU: rasterizador por software con z-buffer,
// textura base, color de vértice y una luz. Proyección ortográfica que
// encuadra el modelo entero.

#include "nectar_assets/image.h"
#include "nectar_assets/mod.h"

namespace nectar {

struct RenderOptions {
	int size      = 128;   ///< lado de la imagen
	float yaw     = -35.0f; ///< grados, giro alrededor de Y
	float pitch   = 25.0f;  ///< grados, inclinación hacia abajo (90 = cenital)
	int supersample = 2;   ///< antialiasing: se dibuja a size*supersample
};

/// Imagen con fondo transparente; vacía si el modelo no tiene nada que dibujar.
Image renderModel(const Model& model, const RenderOptions& options = RenderOptions());

} // namespace nectar
