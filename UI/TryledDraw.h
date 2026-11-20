#pragma once

// Tryled: drawing helpers for the Tryled look (rounded shapes, soft glows), built from plain
// triangles on top of PPSSPP's UI draw buffer, so no new atlas images are needed.

#include <cstdint>

#include "Common/Math/geom2d.h"

class UIContext;
namespace Draw {
class Texture;
}

namespace TryledDraw {

// Filled rectangle with rounded corners. radius is clamped to half the smaller side.
void FillRoundRect(UIContext &dc, const Bounds &b, float radius, uint32_t color);

// Outline of a rounded rectangle, drawn inside the bounds.
void StrokeRoundRect(UIContext &dc, const Bounds &b, float radius, float thickness, uint32_t color);

// Soft glow around a rounded rectangle (drawn outside it).
void Glow(UIContext &dc, const Bounds &b, float radius, float size, uint32_t color);

// A texture drawn into a rounded rectangle, using the given texture coordinates.
void TextureRoundRect(UIContext &dc, Draw::Texture *tex, const Bounds &b, float radius, uint32_t color,
	float u1 = 0.0f, float v1 = 0.0f, float u2 = 1.0f, float v2 = 1.0f);

// Ellipse filled with a radial gradient, from colorCenter in the middle to colorEdge at the rim.
void RadialEllipse(UIContext &dc, float cx, float cy, float rx, float ry, uint32_t colorCenter, uint32_t colorEdge);

// Rectangle with a vertical color gradient, from colorTop to colorBottom.
void VGradient(UIContext &dc, const Bounds &b, uint32_t colorTop, uint32_t colorBottom);

// Texture coordinates that crop a texture to fill the given aspect ratio (like CSS object-fit: cover).
void CoverUV(float texAspect, float boxAspect, float *u1, float *v1, float *u2, float *v2);

}  // namespace TryledDraw

// Tryled: shared views for the Tryled look.
#include "Common/UI/View.h"

// The main action button ("Continuar", "Jugar"...): a pill in the theme accent color.
class TryledPrimaryChoice : public UI::Choice {
public:
	TryledPrimaryChoice(std::string_view text, ImageID image, UI::LayoutParams *layoutParams = nullptr)
		: UI::Choice(text, image, layoutParams) {}
	void Draw(UIContext &dc) override;
};
