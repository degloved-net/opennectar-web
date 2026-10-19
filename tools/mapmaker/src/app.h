#pragma once

// Nectar Map Maker: pantalla de inicio (pide la carpeta assets y prepara el
// catálogo), bienvenida y el editor. Pensado para que cualquiera lo entienda:
// herramientas con nombres claros, ayuda en pantalla, tutorial y una lista de
// lo que falta para poder jugar.

#include "config.h"
#include "course_import.h"
#include "templates.h"
#include "document.h"
#include "math3d.h"
#include "preview.h"
#include "renderer.h"

#include "nectar_assets/catalog.h"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct ImVec2;

namespace nm {

class App {
public:
	App();
	~App();
	/// `openPath`: un map.json para abrir directamente (vacío = bienvenida).
	bool init(const std::string& openPath, std::string& error);
	/// Un fotograma de interfaz (entre NewFrame y Render de ImGui).
	void frame();
	/// La ventana pide cerrarse: true si se puede (si no, pregunta).
	bool requestQuit();
	bool wantsQuit() const { return quit_; }

private:
	enum class Screen { Setup, Building, Welcome, Editor };
	enum class Tool { Move, Place, Floor, Height, Plateau, Paint, Water, Walls, Ramp, Scatter };
	enum class TransformMode { Move, Rotate, Scale };
	enum class HeightOp { Raise, Lower, Flatten, Smooth };
	enum class BrushShape { Circle, Square };
	enum class BrushFalloff { Smooth, Hard };

	struct Rect {
		float x = 0, y = 0, w = 0, h = 0;
	};
	struct Toast {
		int level; // 0 bien, 1 aviso, 2 error
		std::string text;
		double until;
	};
	enum class IssueFix { None, ClampInside, NearestGround, DeleteItem, NormalizeScale };
	struct Issue {
		bool blocking;
		std::string text;
		bool hasItem;
		ItemRef item;
		IssueFix fix = IssueFix::None;
	};
	/// Lo que se coloca con «Poner cosas».
	struct PlaceChoice {
		ItemType type = ItemType::Piece;
		std::string asset; // decoración
		std::string label;
	};

	// ── Pantallas ──
	void drawSetup();
	void drawBuilding();
	void drawWelcome();
	void startCatalog();
	void enterEditor();
	bool shapeButton(const char* id, nectar::TerrainShape shape, const char* label, float size);

	// ── Editor ──
	void drawEditor();
	void drawTopBar(const Rect& r);
	void drawToolbox(const Rect& r);
	void drawViewport(const Rect& r);
	void drawSidePanel(const Rect& r);
	void drawSelected();
	void drawMapPanel();
	void drawMinimap();
	void drawHierarchy();
	void drawChecklist();
	void drawTechnicalReport();
	void drawModals();
	void drawTutorial();
	void drawHelp();
	void drawMaterialBrowser();
	void drawToolOptions();
	void drawPlaceOptions();
	void drawFloorOptions();
	void drawHeightOptions();
	void drawPlateauOptions();
	void drawPaintOptions();
	void drawWaterOptions();
	void drawWallOptions();
	void drawRampOptions();
	void drawScatterOptions();
	void drawBrushSettings();
	void scatter(const V3& at);
	void drawSwatches(bool allowAdd);
	void handleShortcuts();
	const char* toolHelp() const;

	// ── Vista ──
	void renderScene(int w, int h);
	void drawItem(const ItemRef& r, bool ghost);
	void drawSelection();
	void drawSlopes();
	void drawWater();
	void drawRoutes();
	void drawLabels(const ImVec2& origin, const ImVec2& size);
	void drawMoveGizmo(const ImVec2& origin, const ImVec2& size);
	void viewportMouse(const Ray& ray, const ImVec2& origin, const ImVec2& size);
	bool selectionPivot(V3& pivot) const;
	bool selectionCanRotate() const;
	bool selectionCanScale() const;
	V3 moveAxis(int axis) const;
	int moveGizmoAxis(const ImVec2& origin, const ImVec2& size, const ImVec2& mouse) const;
	bool pick(const Ray& ray, ItemRef& hit, float& t, int cycle = 0);
	bool groundHit(const Ray& ray, V3& hit) const;
	bool terrainRay(const Ray& ray, V3& hit) const;
	float docHeight(float x, float z) const;
	bool docSolid(float x, float z) const;
	bool project(const V3& p, const ImVec2& origin, const ImVec2& size, ImVec2& out) const;

	// ── Terreno ──
	bool ensureTerrain();
	void applyBrush(const V3& at, float dt);
	bool applyRamp(const V3& from, const V3& to);
	void growMap(int dx, int dz);
	std::shared_ptr<nectar::Model> courseModel(const std::string& source);
	unsigned swatch(const std::string& source, int index);

	// ── Recursos ──
	std::shared_ptr<GpuModel> model(const std::string& asset);
	std::string markerAsset(const ItemRef& r) const;
	std::string placeAsset(const PlaceChoice& c) const;
	Rgba markerTint(const ItemRef& r) const;
	M4 itemMatrix(const ItemRef& r, bool twin) const;
	unsigned thumb(const std::string& path);
	unsigned assetThumb(const std::string& asset);

	// ── Archivo ──
	void newMap(nectar::TerrainShape shape);
	bool coursePickers();
	bool templatePickers(float size);
	unsigned templateThumb(const TemplateInfo& t);
	bool courseMode() const { return !ed_.doc.course.empty(); }
	void openMap(const std::string& path);
	void openBuiltIn();
	bool saveMap(const std::string& path);
	void saveOrAsk(bool thenTest);
	void testInGame();
	void autosave();
	void notify(int level, const std::string& text);
	void refreshOpenList();

	// ── Edición ──
	void deleteSelection();
	void duplicateSelection();
	void copySelection();
	void paste();
	void focusSelection();
	void rotateSelection(double degrees);
	void liftSelection(double dy);
	void placeSelectionOnGround();
	bool selectionCanGround() const;
	bool itemHidden(const ItemRef& r) const;
	bool itemLocked(const ItemRef& r) const;
	void toggleHidden(const ItemRef& r);
	void toggleLocked(const ItemRef& r);
	void remapSessionItemsAfterRemoval(const std::vector<ItemRef>& removed);
	void placeAt(const V3& at);
	std::vector<Issue> checkMap() const;
	void applyIssueFix(const Issue& issue);
	void createGroundUnder(const ItemRef& item);
	double snap(double v) const;

	Config cfg_;
	Renderer renderer_;
	RenderTarget target_;
	Camera cam_;
	Screen screen_ = Screen::Setup;
	bool quit_ = false, confirmQuit_ = false;

	// Inicio
	char assetsBuf_[1024] = {};
	char gameBuf_[1024]   = {};

	// Catálogo
	nectar::Catalog catalog_;
	std::thread catalogThread_;
	std::atomic<int> catalogDone_ { 0 }, catalogTotal_ { 0 };
	std::atomic<bool> catalogFinished_ { false };
	std::string catalogError_;
	std::map<std::string, unsigned> thumbs_;
	int thumbLoadsThisFrame_ = 0;
	char search_[128]        = {};
	char hierarchySearch_[128] = {};

	// Modelos ya subidos a la GPU (marcadores y decoración).
	std::map<std::string, std::shared_ptr<GpuModel>> models_;
	std::map<std::string, std::string> modelErrors_;

	// Documento
	EditorDoc ed_;
	nectar::MapDoc clipboard_;
	bool hasClipboard_ = false;
	Xform transformClipboard_;
	ItemCaps transformClipboardCaps_;
	bool hasTransformClipboard_ = false;
	Preview preview_;
	PreviewResult compiled_;
	std::shared_ptr<GpuModel> baseGpu_;
	std::vector<V3> waterTris_; ///< superficie del agua del mapa compilado
	std::vector<V3> collisionTris_; ///< geometría compilada que usa el juego
	CourseField courseField_;   ///< suelo del escenario del juego (mapas con escenario)
	int requestedLive_  = -1;
	double lastRequest_ = 0;
	std::vector<Toast> toasts_;
	double lastAutosave_ = 0;

	// Herramientas
	Tool tool_         = Tool::Move;
	TransformMode transformMode_ = TransformMode::Move;
	HeightOp heightOp_ = HeightOp::Raise;
	BrushShape brushShape_ = BrushShape::Circle;
	BrushFalloff brushFalloff_ = BrushFalloff::Smooth;
	float brushRadius_ = 150.0f, brushStrength_ = 150.0f, flattenTarget_ = 0.0f;
	bool floorErase_ = false, brushValid_ = false, brushWasDown_ = false;
	bool waterErase_ = false, plateauUp_ = true;
	float plateauAmount_ = 100.0f;
	int paintMaterial_ = 0;
	V3 brushPos_;
	PlaceChoice place_;
	bool placeValid_ = false;
	V3 placePos_;
	float wallHeight_ = 150.0f, wallThickness_ = 40.0f;
	// Pincel de decoración
	std::vector<std::string> scatterSet_ { "objects/weeds/weed1.mod", "objects/plants/hutaba.mod", "objects/plants/hae.mod" };
	float scatterDensity_ = 6.0f, scatterSizeVar_ = 0.25f;
	bool scatterErase_ = false, scatterCollide_ = false, scatterFullWarned_ = false;
	unsigned scatterSeed_ = 12345u;
	bool drawingWall_ = false;
	V3 wallStart_;
	bool drawingRamp_ = false, rampPreviewValid_ = false;
	V3 rampStart_, rampEnd_;
	float rampWidth_ = 250.0f;
	bool rampSmooth_ = true;
	// Arrastre con «Mover»
	bool dragging_ = false, dragArmed_ = false;
	bool boxSelecting_ = false;
	int dragHandle_ = -1; // extremo del muro que se arrastra (0 A, 1 B) o -1
	int dragAxis_ = 0; ///< 0 libre, 1 eje X, 2 eje Z, 3 giro Y, 4 escala uniforme
	V3 dragGrab_;
	V3 transformPivot_;
	float rotateStartAngle_ = 0;
	float scaleStartDistance_ = 1;
	float pressX_ = 0, pressY_ = 0, boxEndX_ = 0, boxEndY_ = 0;
	float lastAltPickX_ = -1000, lastAltPickY_ = -1000;
	int altPickCycle_ = 0;
	std::vector<std::pair<ItemRef, Xform>> dragStart_;
	std::vector<ItemRef> boxSelectionBase_;
	std::vector<ItemRef> hiddenItems_, lockedItems_; ///< solo sesión; nunca se guardan en map.json

	// Materiales
	bool openMatBrowser_  = false;
	int matBrowserTarget_ = -1; // -1 añadir a la paleta, -2 paredes del borde, k cambiar el k
	std::string matBrowserCourse_;
	std::map<std::string, std::shared_ptr<nectar::Model>> courseModels_;
	std::map<std::string, unsigned> swatches_;
	std::map<std::string, std::vector<std::array<int, 3>>> courseStats_; // (material, triángulos, de suelo)

	// Vista
	bool showTwins_ = true, mirrorEdits_ = true;
	bool showCollision_ = false, showRoutes_ = false, showSlopesOverlay_ = false, showWaterOverlay_ = true, showLabels_ = true;
	bool viewportHovered_ = false;
	int splitterDrag_     = 0; ///< 0 ninguno, 1 herramientas, 2 propiedades
	float viewAspect_     = 1.0f;
	Rect viewRect_, toolboxRect_, checklistRect_, testButtonRect_, floorButtonRect_, placeButtonRect_;

	// Diálogos y ayuda
	bool openSaveAs_ = false, openOpen_ = false, openNew_ = false, openHelp_ = false, testAfterSave_ = false;
	char saveName_[128] = {};
	std::vector<std::pair<std::string, std::string>> openList_; // (nombre, ruta)
	int tutorialStep_ = -1;
	std::string openAtStart_;
};

} // namespace nm
