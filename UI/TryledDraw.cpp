// Tryled: drawing helpers. See TryledDraw.h.

#include <algorithm>
#include <cmath>
#include <vector>

#include "Common/GPU/thin3d.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "UI/TryledDraw.h"

namespace TryledDraw {

static const int kCornerSegments = 8;

struct Pt {
	float x, y;
};

// Points around a rounded rectangle, clockwise from the top-left corner.
static void Perimeter(const Bounds &b, float radius, std::vector<Pt> *out) {
	radius = std::max(0.0f, std::min(radius, std::min(b.w, b.h) * 0.5f));
	out->clear();
	const float cx[4] = { b.x + radius, b.x2() - radius, b.x2() - radius, b.x + radius };
	const float cy[4] = { b.y + radius, b.y + radius, b.y2() - radius, b.y2() - radius };
	const float start[4] = { (float)M_PI, (float)M_PI * 1.5f, 0.0f, (float)M_PI * 0.5f };
	for (int c = 0; c < 4; c++) {
		if (radius <= 0.0f) {
			out->push_back({ cx[c], cy[c] });
			continue;
		}
		for (int i = 0; i <= kCornerSegments; i++) {
			const float a = start[c] + (float)M_PI * 0.5f * i / kCornerSegments;
			out->push_back({ cx[c] + cosf(a) * radius, cy[c] + sinf(a) * radius });
		}
	}
}

// UV of a solid white texel in the UI atlas, so plain color fills work with the UI texture bound.
static void WhiteUV(UIContext &dc, float *u, float *v) {
	const AtlasImage *img = dc.Draw()->GetAtlas()->getImage(dc.GetTheme().whiteImage);
	if (img) {
		*u = (img->u1 + img->u2) * 0.5f;
		*v = (img->v1 + img->v2) * 0.5f;
	} else {
		*u = 0.0f;
		*v = 0.0f;
	}
}

void FillRoundRect(UIContext &dc, const Bounds &b, float radius, uint32_t color) {
	if (b.w <= 0.0f || b.h <= 0.0f || (color >> 24) == 0)
		return;
	std::vector<Pt> pts;
	Perimeter(b, radius, &pts);
	float u, v;
	WhiteUV(dc, &u, &v);
	DrawBuffer *db = dc.Draw();
	const float mx = b.centerX(), my = b.centerY();
	for (size_t i = 0; i < pts.size(); i++) {
		const Pt &p0 = pts[i];
		const Pt &p1 = pts[(i + 1) % pts.size()];
		db->V(mx, my, color, u, v);
		db->V(p0.x, p0.y, color, u, v);
		db->V(p1.x, p1.y, color, u, v);
	}
}

void StrokeRoundRect(UIContext &dc, const Bounds &b, float radius, float thickness, uint32_t color) {
	if (b.w <= 0.0f || b.h <= 0.0f || thickness <= 0.0f || (color >> 24) == 0)
		return;
	std::vector<Pt> outer, inner;
	Perimeter(b, radius, &outer);
	Bounds ib(b.x + thickness, b.y + thickness, b.w - 2.0f * thickness, b.h - 2.0f * thickness);
	if (ib.w <= 0.0f || ib.h <= 0.0f) {
		FillRoundRect(dc, b, radius, color);
		return;
	}
	Perimeter(ib, std::max(0.0f, radius - thickness), &inner);
	float u, v;
	WhiteUV(dc, &u, &v);
	DrawBuffer *db = dc.Draw();
	const size_t n = std::min(outer.size(), inner.size());
	for (size_t i = 0; i < n; i++) {
		const size_t j = (i + 1) % n;
		db->V(outer[i].x, outer[i].y, color, u, v);
		db->V(outer[j].x, outer[j].y, color, u, v);
		db->V(inner[j].x, inner[j].y, color, u, v);
		db->V(outer[i].x, outer[i].y, color, u, v);
		db->V(inner[j].x, inner[j].y, color, u, v);
		db->V(inner[i].x, inner[i].y, color, u, v);
	}
}

void Glow(UIContext &dc, const Bounds &b, float radius, float size, uint32_t color) {
	const int steps = 8;
	const float alpha = (float)(color >> 24) / 255.0f;
	const float step = size / steps;
	for (int i = 0; i < steps; i++) {
		const float t = (float)i / steps;
		const float a = alpha * (1.0f - t) * (1.0f - t) * 0.5f;
		const float grow = step * i;
		Bounds g(b.x - grow - step, b.y - grow - step, b.w + 2.0f * (grow + step), b.h + 2.0f * (grow + step));
		StrokeRoundRect(dc, g, radius + grow + step, step, (color & 0xFFFFFF) | ((uint32_t)(a * 255.0f) << 24));
	}
}

void TextureRoundRect(UIContext &dc, Draw::Texture *tex, const Bounds &b, float radius, uint32_t color, float u1, float v1, float u2, float v2) {
	if (!tex || b.w <= 0.0f || b.h <= 0.0f)
		return;
	std::vector<Pt> pts;
	Perimeter(b, radius, &pts);
	auto uvOf = [&](float x, float y, float *u, float *v) {
		*u = u1 + (u2 - u1) * (x - b.x) / b.w;
		*v = v1 + (v2 - v1) * (y - b.y) / b.h;
	};
	dc.Flush();
	dc.GetDrawContext()->BindTexture(0, tex);
	DrawBuffer *db = dc.Draw();
	float mu, mv;
	uvOf(b.centerX(), b.centerY(), &mu, &mv);
	for (size_t i = 0; i < pts.size(); i++) {
		const Pt &p0 = pts[i];
		const Pt &p1 = pts[(i + 1) % pts.size()];
		float pu0, pv0, pu1, pv1;
		uvOf(p0.x, p0.y, &pu0, &pv0);
		uvOf(p1.x, p1.y, &pu1, &pv1);
		db->V(b.centerX(), b.centerY(), color, mu, mv);
		db->V(p0.x, p0.y, color, pu0, pv0);
		db->V(p1.x, p1.y, color, pu1, pv1);
	}
	dc.Flush();
	dc.RebindTexture();
}

void RadialEllipse(UIContext &dc, float cx, float cy, float rx, float ry, uint32_t colorCenter, uint32_t colorEdge) {
	float u, v;
	WhiteUV(dc, &u, &v);
	DrawBuffer *db = dc.Draw();
	const int segments = 48;
	for (int i = 0; i < segments; i++) {
		const float a0 = (float)M_PI * 2.0f * i / segments;
		const float a1 = (float)M_PI * 2.0f * (i + 1) / segments;
		db->V(cx, cy, colorCenter, u, v);
		db->V(cx + cosf(a0) * rx, cy + sinf(a0) * ry, colorEdge, u, v);
		db->V(cx + cosf(a1) * rx, cy + sinf(a1) * ry, colorEdge, u, v);
	}
}

void CoverUV(float texAspect, float boxAspect, float *u1, float *v1, float *u2, float *v2) {
	*u1 = 0.0f;
	*v1 = 0.0f;
	*u2 = 1.0f;
	*v2 = 1.0f;
	if (texAspect <= 0.0f || boxAspect <= 0.0f)
		return;
	if (texAspect > boxAspect) {
		// Texture is wider: crop the sides.
		const float keep = boxAspect / texAspect;
		*u1 = (1.0f - keep) * 0.5f;
		*u2 = 1.0f - *u1;
	} else {
		const float keep = texAspect / boxAspect;
		*v1 = (1.0f - keep) * 0.5f;
		*v2 = 1.0f - *v1;
	}
}

}  // namespace TryledDraw

void TryledPrimaryChoice::Draw(UIContext &dc) {
	const UI::Theme &t = dc.GetTheme();
	uint32_t bg = t.itemFocusedStyle.background.color;
	uint32_t fg = t.itemFocusedStyle.fgColor;
	if (down_) {
		bg = t.itemDownStyle.background.color;
		fg = t.itemDownStyle.fgColor;
	}
	const Bounds b = ItemBackgroundBounds(dc);
	TryledDraw::Glow(dc, b, b.h * 0.5f, 12.0f, (bg & 0xFFFFFF) | 0x80000000);
	TryledDraw::FillRoundRect(dc, b, b.h * 0.5f, bg);
	if (HasFocus())
		TryledDraw::StrokeRoundRect(dc, b.Expand(3.0f), b.h * 0.5f + 3.0f, 2.0f, t.infoStyle.fgColor);
	float x = b.x + 18.0f;
	if (image_.isValid()) {
		dc.Draw()->DrawImage(image_, x + 12.0f, b.centerY(), 0.8f, fg, ALIGN_CENTER);
		x += 34.0f;
	}
	dc.SetFontStyle(t.uiFont);
	dc.DrawTextRectSqueeze(text_, Bounds(x, b.y, b.x2() - x - 12.0f, b.h), fg, ALIGN_LEFT | ALIGN_VCENTER);
}
