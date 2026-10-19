#include "nectar_assets/ini.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace nectar {

const IniNode* IniNode::find(const std::string& k) const
{
	for (const IniNode& c : children) {
		if (c.key == k) return &c;
	}
	return nullptr;
}

float IniNode::num(std::size_t i, float def) const { return i < args.size() ? float(std::atof(args[i].c_str())) : def; }

namespace {

// Partes de una línea: palabras, cadenas entre comillas simples y llaves.
std::vector<std::string> tokens(const std::string& line)
{
	std::vector<std::string> out;
	std::size_t i = 0;
	while (i < line.size()) {
		const char c = line[i];
		if (c == ' ' || c == '\t' || c == '\r') {
			i++;
		} else if (c == '{' || c == '}') {
			out.push_back(std::string(1, c));
			i++;
		} else if (c == '\'') {
			const std::size_t e = line.find('\'', i + 1);
			out.push_back(line.substr(i + 1, (e == std::string::npos ? line.size() : e) - i - 1));
			i = e == std::string::npos ? line.size() : e + 1;
		} else {
			std::size_t e = i;
			while (e < line.size() && line[e] != ' ' && line[e] != '\t' && line[e] != '\r' && line[e] != '{' && line[e] != '}') e++;
			out.push_back(line.substr(i, e - i));
			i = e;
		}
	}
	return out;
}

Rgba8 colour(const IniNode* n)
{
	Rgba8 c;
	if (!n) return c;
	c.r = std::uint8_t(n->num(0, 255));
	c.g = std::uint8_t(n->num(1, 255));
	c.b = std::uint8_t(n->num(2, 255));
	c.a = std::uint8_t(n->num(3, 255));
	return c;
}

Vec3 vec(const IniNode* n)
{
	Vec3 v;
	if (n) v = { n->num(0), n->num(1), n->num(2) };
	return v;
}

bool readText(const std::string& path, std::string& out, std::string& error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		error = "no se puede abrir " + path;
		return false;
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

} // namespace

bool parseIniTree(const std::string& text, IniNode& root, std::string& error)
{
	root = IniNode();
	root.block = true;
	std::vector<IniNode*> stack { &root };
	std::istringstream in(text);
	std::string line;
	int lineNo = 0;
	while (std::getline(in, line)) {
		lineNo++;
		const std::size_t cmt = line.find("//");
		if (cmt != std::string::npos) line.resize(cmt);
		const std::size_t nul = line.find('\0');
		if (nul != std::string::npos) line.resize(nul);
		std::vector<std::string> tk = tokens(line);
		std::size_t i = 0;
		while (i < tk.size()) {
			if (tk[i] == "}") {
				if (stack.size() == 1) {
					error = "llave } de más en la línea " + std::to_string(lineNo);
					return false;
				}
				stack.pop_back();
				i++;
				continue;
			}
			// Una entrada: clave + argumentos hasta { o } o fin de línea.
			IniNode node;
			node.key = tk[i++];
			while (i < tk.size() && tk[i] != "{" && tk[i] != "}") node.args.push_back(tk[i++]);
			IniNode* parent = stack.back();
			parent->children.push_back(node);
			if (i < tk.size() && tk[i] == "{") {
				parent->children.back().block = true;
				stack.push_back(&parent->children.back());
				i++;
			}
		}
	}
	if (stack.size() != 1) {
		error = "falta cerrar una llave {";
		return false;
	}
	return true;
}

bool parseScene(const std::string& text, Scene& out, std::string& error)
{
	IniNode root;
	if (!parseIniTree(text, root, error)) return false;
	out = Scene();
	if (const IniNode* n = root.find("map_file")) out.mapFile = n->args.empty() ? "" : n->args[0];
	if (const IniNode* n = root.find("navi_start")) out.naviStart = { n->num(0), n->num(1) };
	if (const IniNode* n = root.find("day_multiply")) out.dayMultiply = n->num(0, 1.0f);
	if (const IniNode* day = root.find("dayMgr")) {
		for (const IniNode& t : day->children) {
			if (t.key != "timesetting" || !t.block) continue;
			SceneTime time;
			for (const IniNode& c : t.children) {
				if (c.key == "light" && c.block) {
					SceneLight l;
					if (const IniNode* n = c.find("type")) l.type = int(n->num(0));
					if (const IniNode* n = c.find("attach")) l.attach = int(n->num(0));
					if (const IniNode* n = c.find("fov")) l.fov = n->num(0);
					l.position  = vec(c.find("position"));
					l.direction = vec(c.find("direction"));
					l.colour    = colour(c.find("colour"));
					time.lights.push_back(l);
				} else if (c.key == "ambient" && c.block) {
					time.ambient = colour(c.find("colour"));
				} else if (c.key == "fog" && c.block) {
					time.fogColour = colour(c.find("colour"));
					if (const IniNode* n = c.find("dist")) {
						time.fogNear = n->num(0);
						time.fogFar  = n->num(1);
					}
				}
			}
			out.times.push_back(time);
		}
	}
	if (out.mapFile.empty()) {
		error = "no tiene map_file";
		return false;
	}
	return true;
}

bool loadScene(const std::string& path, Scene& out, std::string& error)
{
	std::string text;
	return readText(path, text, error) && parseScene(text, out, error);
}

bool parseRoutes(const std::string& text, Routes& out, std::string& error)
{
	IniNode root;
	if (!parseIniTree(text, root, error)) return false;
	out = Routes();
	for (const IniNode& route : root.children) {
		if (route.key != "route" || !route.block) continue;
		for (const IniNode& c : route.children) {
			if (c.key == "point" && c.block) {
				RoutePoint p;
				if (const IniNode* n = c.find("index")) p.index = int(n->num(0));
				p.pos = vec(c.find("pos"));
				if (const IniNode* n = c.find("width")) p.width = n->num(0);
				out.points.push_back(p);
			} else if (c.key == "link" && c.block) {
				// "link { a b }": los números quedan como hijos sin valor.
				if (c.children.size() == 1 && c.children[0].args.size() == 1) {
					out.links.push_back({ std::atoi(c.children[0].key.c_str()), std::atoi(c.children[0].args[0].c_str()) });
				}
			}
		}
	}
	return true;
}

} // namespace nectar
