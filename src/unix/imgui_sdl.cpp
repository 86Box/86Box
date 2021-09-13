#include "imgui_sdl.h"

#include "SDL.h"

#include "imgui.h"

#include <map>
#include <list>
#include <cmath>
#include <array>
#include <vector>
#include <memory>
#include <iostream>
#include <algorithm>
#include <functional>
#include <unordered_map>

namespace
{
	struct Device* CurrentDevice = nullptr;

	namespace TupleHash
	{
		template <typename T> struct Hash
		{
			std::size_t operator()(const T& value) const
			{
				return std::hash<T>()(value);
			}
		};

		template <typename T> void CombineHash(std::size_t& seed, const T& value)
		{
			seed ^= TupleHash::Hash<T>()(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		}

		template <typename Tuple, std::size_t Index = std::tuple_size<Tuple>::value - 1> struct Hasher
		{
			static void Hash(std::size_t& seed, const Tuple& tuple)
			{
				Hasher<Tuple, Index - 1>::Hash(seed, tuple);
				CombineHash(seed, std::get<Index>(tuple));
			}
		};

		template <typename Tuple> struct Hasher<Tuple, 0>
		{
			static void Hash(std::size_t& seed, const Tuple& tuple)
			{
				CombineHash(seed, std::get<0>(tuple));
			}
		};

		template <typename... T> struct Hash<std::tuple<T...>>
		{
			std::size_t operator()(const std::tuple<T...>& value) const
			{
				std::size_t seed = 0;
				Hasher<std::tuple<T...>>::Hash(seed, value);
				return seed;
			}
		};
	}

	template <typename Key, typename Value> class LRUCache
	{
	public:
		void SetCapacity(size_t capacity) { Capacity = capacity; }

		bool Contains(const Key& key) const
		{
			return Container.find(key) != Container.end();
		}

		const Value& At(const Key& key)
		{
			assert(Contains(key));

			const auto location = Container.find(key);
			Order.splice(Order.begin(), Order, location->second);
			return location->second->second;
		}

		void Insert(const Key& key, Value value)
		{
			const auto existingLocation = Container.find(key);
			if (existingLocation != Container.end())
			{
				Order.erase(existingLocation->second);
				Container.erase(existingLocation);
			}

			Order.push_front(std::make_pair(key, std::move(value)));
			Container.insert(std::make_pair(key, Order.begin()));
		}

		void Reset()
		{
			Order.clear();
			Container.clear();
		}

		void Clean()
		{
			while (Container.size() > Capacity)
			{
				auto last = Order.end();
				last--;
				Container.erase(last->first);
				Order.pop_back();
			}
		}

	private:
		size_t Capacity = 0;
		std::list<std::pair<Key, Value>> Order;
		std::unordered_map<Key, decltype(Order.begin()), TupleHash::Hash<Key>> Container;
	};

	struct Color
	{
		const float R, G, B, A;

		explicit Color(uint32_t color)
			: R(((color >> 0) & 0xff) / 255.0f), G(((color >> 8) & 0xff) / 255.0f), B(((color >> 16) & 0xff) / 255.0f), A(((color >> 24) & 0xff) / 255.0f) { }
		Color(float r, float g, float b, float a) : R(r), G(g), B(b), A(a) { }

		Color operator*(const Color& c) const { return Color(R * c.R, G * c.G, B * c.B, A * c.A); }
		Color operator*(float v) const { return Color(R * v, G * v, B * v, A * v); }
		Color operator+(const Color& c) const { return Color(R + c.R, G + c.G, B + c.B, A + c.A); }

		uint32_t ToInt() const
		{
			return	((static_cast<int>(R * 255) & 0xff) << 0)
				  | ((static_cast<int>(G * 255) & 0xff) << 8)
				  | ((static_cast<int>(B * 255) & 0xff) << 16)
				  | ((static_cast<int>(A * 255) & 0xff) << 24);
		}

		void UseAsDrawColor(SDL_Renderer* renderer) const
		{
			SDL_SetRenderDrawColor(renderer,
				static_cast<uint8_t>(R * 255),
				static_cast<uint8_t>(G * 255),
				static_cast<uint8_t>(B * 255),
				static_cast<uint8_t>(A * 255));
		}
	};

	struct Device
	{
		SDL_Renderer* Renderer;
		bool CacheWasInvalidated = false;

		struct ClipRect
		{
			int X, Y, Width, Height;
		} Clip;

		struct TriangleCacheItem
		{
			SDL_Texture* Texture = nullptr;
			int Width = 0, Height = 0;

			~TriangleCacheItem() { if (Texture) SDL_DestroyTexture(Texture); }
		};

		// The triangle cache has to be basically a full representation of the triangle.
		// This includes the (offset) vertex positions, texture coordinates and vertex colors.
		using GenericTriangleVertexKey = std::tuple<float, float, float, float, uint32_t>;
		using GenericTriangleKey = std::tuple<GenericTriangleVertexKey, GenericTriangleVertexKey, GenericTriangleVertexKey, SDL_Texture*>;

		LRUCache<GenericTriangleKey, std::unique_ptr<TriangleCacheItem>> TriangleCache;

		Device(SDL_Renderer* renderer) : Renderer(renderer) { }

		void SetClipRect(const ClipRect& rect)
		{
			Clip = rect;
			const SDL_Rect clip = { rect.X, rect.Y, rect.Width, rect.Height };
			SDL_RenderSetClipRect(Renderer, &clip);
		}

		void EnableClip() { SetClipRect(Clip); }
		void DisableClip() { SDL_RenderSetClipRect(Renderer, nullptr); }

		SDL_Texture* MakeTexture(int width, int height)
		{
			SDL_Texture* texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
			SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
			return texture;
		}

		void UseAsRenderTarget(SDL_Texture* texture)
		{
			SDL_SetRenderTarget(Renderer, texture);
			if (texture)
			{
				SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 0);
				SDL_RenderClear(Renderer);
			}
		}
	};

	struct Rect
	{
		float MinX, MinY, MaxX, MaxY;
		float MinU, MinV, MaxU, MaxV;

		bool IsOnExtreme(const ImVec2& point) const
		{
			return (point.x == MinX || point.x == MaxX) && (point.y == MinY || point.y == MaxY);
		}

		bool UsesOnlyColor() const
		{
			const ImVec2& whitePixel = ImGui::GetIO().Fonts->TexUvWhitePixel;

			return MinU == MaxU && MinU == whitePixel.x && MinV == MaxV && MaxV == whitePixel.y;
		}

		static Rect CalculateBoundingBox(const ImDrawVert& v0, const ImDrawVert& v1, const ImDrawVert& v2)
		{
			return Rect{
				std::min({ v0.pos.x, v1.pos.x, v2.pos.x }),
				std::min({ v0.pos.y, v1.pos.y, v2.pos.y }),
				std::max({ v0.pos.x, v1.pos.x, v2.pos.x }),
				std::max({ v0.pos.y, v1.pos.y, v2.pos.y }),
				std::min({ v0.uv.x, v1.uv.x, v2.uv.x }),
				std::min({ v0.uv.y, v1.uv.y, v2.uv.y }),
				std::max({ v0.uv.x, v1.uv.x, v2.uv.x }),
				std::max({ v0.uv.y, v1.uv.y, v2.uv.y })
			};
		}
	};

	void DrawTriangle(ImDrawVert v1, ImDrawVert v2, ImDrawVert v3, SDL_Texture *texture)
	{
		// This function operates in s28.4 fixed point: it's more precise than
		// floating point and often faster. This also effectively lets us work in
		// a subpixel space where each pixel is divided into 256 subpixels.
		// TODO: check for overflow

		// Find integral bounding box, scale to fixed point. We use this to
		// iterate over all pixels possibly covered by the triangle.
		const Sint32 minXf = SDL_floor(SDL_min(v1.pos.x, SDL_min(v2.pos.x, v3.pos.x))) * 16;
		const Sint32 minYf = SDL_floor(SDL_min(v1.pos.y, SDL_min(v2.pos.y, v3.pos.y))) * 16;
		const Sint32 maxXf = SDL_ceil(SDL_max(v1.pos.x, SDL_max(v2.pos.x, v3.pos.x))) * 16;
		const Sint32 maxYf = SDL_ceil(SDL_max(v1.pos.y, SDL_max(v2.pos.y, v3.pos.y))) * 16;

		// Find center of bounding box, used for translating coordinates.
		// This accomplishes two things: 1) makes the fixed point calculations less
		// likely to overflow 2) makes it less likely that triangles will be rendered
		// differently on different parts of the screen (seems to happen sometimes
		// with fractional coordinates). Make sure they're on integer boundaries too,
		// to make it easy to calculate our starting position.
		const Sint32 meanXf = round((maxXf - minXf) / 2 / 16) * 16;
		const Sint32 meanYf = round((maxYf - minYf) / 2 / 16) * 16;

		// Translate vertex coordinates with respect to the center of the bounding
		// box, and scale to fixed point.
		Sint32 f1x = round(v1.pos.x * 16) - meanXf;
		Sint32 f1y = round(v1.pos.y * 16) - meanYf;
		Sint32 f2x = round(v2.pos.x * 16) - meanXf;
		Sint32 f2y = round(v2.pos.y * 16) - meanYf;
		Sint32 f3x = round(v3.pos.x * 16) - meanXf;
		Sint32 f3y = round(v3.pos.y * 16) - meanYf;

		// Calculate starting position for iteration. It's the top-left of our
		// bounding box with respect to the center of the bounding box. We add a
		// half-pixel on each axis to match hardware renderers, which evaluate at the
		// center of pixels.
		const Sint32 px = minXf - meanXf + 8;
		const Sint32 py = minYf - meanYf + 8;

		// Calculate barycentric coordinates at starting position. Barycentric
		// coordinates tell us the position of a point with respect to the
		// edges/vertices of a triangle: we can easily use these to calculate if
		// a point is inside a triangle (the three barycentric coordinates will all
		// be positive) and how to interpolate vertex attributes (multiply them by
		// the normalized barycentric coordinates at that point.)
		Sint32 w1 = (f3x - f2x) * (py - f2y) - (f3y - f2y) * (px - f2x);
		Sint32 w2 = (f1x - f3x) * (py - f3y) - (f1y - f3y) * (px - f3x);
		Sint32 w3 = (f2x - f1x) * (py - f1y) - (f2y - f1y) * (px - f1x);

		// Calculate the normalization factor for transforming our barycentric
		// coordinates into interpolation constants. If it's negative, then the
		// triangle is back-facing (wound the wrong way), and we flip two vertices
		// to make it front-facing. Keep this factor as a float since 1) we'll be
		// dividing by it later 2) we don't lose precision going through the raster
		// loop.
		float normalization = (w1 + w2 + w3);
		if (normalization < 0) {
			ImDrawVert vswap = v3;
			v3 = v2;
			v2 = vswap;

			Sint32 fxswap = f3x;
			f3x = f2x;
			f2x = fxswap;
			Sint32 fyswap = f3y;
			f3y = f2y;
			f2y = fyswap;

			Sint32 wswap = w3;
			w3 = -w2;
			w2 = -wswap;
			w1 = -w1;

			normalization = -normalization;
		}

		// We deal with shared edges between triangles by defining a fill rule: only
		// edges on the top or left of the triangle will be filled. We could change
		// the comparison in the loop below to differentiate between greater-than
		// and greater-or-equal-than, but since we're in fixed point space where
		// everything is an integer we instead add a bias to each barycentric
		// coordinate corresponding to a non-top, non-left edge.
		const int bias1 = ((f3y == f2y && f3x > f2x) || f3x < f2x) ? 0 : -1;
		const int bias2 = ((f3y == f1y && f1x > f3x) || f1x < f3x) ? 0 : -1;
		const int bias3 = ((f2y == f1y && f2x > f1x) || f2x < f1x) ? 0 : -1;
		w1 += bias1;
		w2 += bias2;
		w3 += bias3;

		// As we go through each pixel, we use the barycentric coordinates to check
		// if they're covered by the triangle. We could recalculate them every time,
		// but since they're linear we can calculate the linear factors with respect
		// to advancing through columns and rows and just add each time through the
		// loop. We multiply to get from subpixel space back to pixel space, since
		// we'll be iterating pixel by pixel.
		const Sint32 a1 = (f2y - f3y) * 16;
		const Sint32 a2 = (f3y - f1y) * 16;
		const Sint32 a3 = (f1y - f2y) * 16;
		const Sint32 b1 = (f3x - f2x) * 16;
		const Sint32 b2 = (f1x - f3x) * 16;
		const Sint32 b3 = (f2x - f1x) * 16;

		// Save the original texture color and alpha mod here, since we change it
		// according to vertex attributes and need to return it to its original state
		// afterwards.
		Uint8 original_mod_r, original_mod_g, original_mod_b, original_mod_a;
		if (texture) {
			SDL_GetTextureColorMod(texture, &original_mod_r, &original_mod_g, &original_mod_b);
			SDL_GetTextureAlphaMod(texture, &original_mod_a);
		}

		// Store texture width and height for use in mapping vertex attributes
		int texture_width = 0, texture_height = 0;
		if (texture) {
			SDL_QueryTexture(texture, NULL, NULL, &texture_width, &texture_height);
		}

		// Precalculate normalized vertex attributes. We just need to multiply these
		// by the barycentric coordinates and sum them to get the interpolated vertex
		// attribute for any point. This can save a few frames per second.
		const float col1r = Color(v1.col).R * 255 / normalization;
		const float col1g = Color(v1.col).G * 255 / normalization;
		const float col1b = Color(v1.col).B * 255 / normalization;
		const float col1a = Color(v1.col).A * 255 / normalization;
		const float col2r = Color(v2.col).R * 255 / normalization;
		const float col2g = Color(v2.col).G * 255 / normalization;
		const float col2b = Color(v2.col).B * 255 / normalization;
		const float col2a = Color(v2.col).A * 255 / normalization;
		const float col3r = Color(v3.col).R * 255 / normalization;
		const float col3g = Color(v3.col).G * 255 / normalization;
		const float col3b = Color(v3.col).B * 255 / normalization;
		const float col3a = Color(v3.col).A * 255 / normalization;
		const float v1u = v1.uv.x * texture_width / normalization;
		const float v1v = v1.uv.y * texture_height / normalization;
		const float v2u = v2.uv.x * texture_width / normalization;
		const float v2v = v2.uv.y * texture_height / normalization;
		const float v3u = v3.uv.x * texture_width / normalization;
		const float v3v = v3.uv.y * texture_height / normalization;

		// If the triangle is uniformly-colored, we can get a big speed up by setting
		// the color once and drawing batches of rows, rather than drawing individually
		// colored pixels. Avoid malloc and a dynamic buffer size since it's slower
		// than just grabbing space from the stack.
		bool isUniformColor = false;
		SDL_Rect rectsbuffer[1024];
		int rects_i = 0;
		if (!texture && v1.col == v2.col && v1.col == v3.col) {
			isUniformColor = true;
			SDL_SetRenderDrawColor(CurrentDevice->Renderer,
				Color(v1.col).R * 255,
				Color(v1.col).G * 255,
				Color(v1.col).B * 255,
				Color(v1.col).A * 255
			);
		}

		// Iterate over all pixels in the bounding box.
		for (int y = minYf / 16; y <= maxYf / 16; y++) {
			// Stash barycentric coordinates at start of row
			Sint32 w1_row = w1;
			Sint32 w2_row = w2;
			Sint32 w3_row = w3;

			// Keep track of where the triangle starts on this row, as an optimization
			// to draw uniformly-colored triangles.
			bool in_triangle = false;
			int x_start;

			for (int x = minXf / 16; x <= maxXf / 16; x++) {
				// If all barycentric coordinates are positive, we're inside the triangle
				if (w1 >= 0 && w2 >= 0 && w3 >= 0) {
					if (!in_triangle) {
						// We draw uniformly-colored triangles row by row, so we need to keep
						// track of where the row starts and know when it ends.
						x_start = x;
						in_triangle = true;
					}

					if (!isUniformColor) {
						// Fix the adjustment due to fill rule. It's incorrect when calculating
						// interpolation values.
						const Sint32 alpha = w1 - bias1;
						const Sint32 beta = w2 - bias2;
						const Sint32 gamma = w3 - bias3;

						// Interpolate color
						const Uint8 r = col1r * alpha + col2r * beta + col3r * gamma;
						const Uint8 g = col1g * alpha + col2g * beta + col3g * gamma;
						const Uint8 b = col1b * alpha + col2b * beta + col3b * gamma;
						const Uint8 a = col1a * alpha + col2a * beta + col3a * gamma;

						if (!texture) {
							// Draw a single colored pixel
							SDL_SetRenderDrawColor(CurrentDevice->Renderer, r, g, b, a);
							SDL_RenderDrawPoint(CurrentDevice->Renderer, x, y);
						} else {
							// Copy a pixel from the source texture to the target pixel. This
							// effectively does nearest neighbor sampling. Could probably be
							// extended to copy from a larger rect to do bilinear sampling if
							// needed.
							const int u = v1u * alpha + v2u * beta + v3u * gamma;
							const int v = v1v * alpha + v2v * beta + v3v * gamma;
							SDL_SetTextureColorMod(texture, r, g, b);
							SDL_SetTextureAlphaMod(texture, a);
							SDL_Rect srcrect;
							srcrect.x = u;
							srcrect.y = v;
							srcrect.w = 1;
							srcrect.h = 1;
							SDL_Rect destrect;
							destrect.x = x;
							destrect.y = y;
							destrect.w = 1;
							destrect.h = 1;
							SDL_RenderCopy(CurrentDevice->Renderer, texture, &srcrect, &destrect);
						}
					}
				} else if (in_triangle) {
					// No longer in triangle, so we're done with this row.
					if (isUniformColor) {
						// For uniformly-colored triangles, store lines so we can send them
						// to the renderer in batches. This provides a huge speedup in most
						// cases (even with SDL 2.0.10's built-in batching!).
						rectsbuffer[rects_i].x = x_start;
						rectsbuffer[rects_i].y = y;
						rectsbuffer[rects_i].w = x - x_start;
						rectsbuffer[rects_i].h = 1;
						rects_i++;
						if (rects_i == 1024) {
							SDL_RenderFillRects(CurrentDevice->Renderer, rectsbuffer, rects_i);
							rects_i = 0;
						}
					}

					break;
				}

				// Increment barycentric coordinates one pixel rightwards
				w1 += a1;
				w2 += a2;
				w3 += a3;
			}
			// Increment barycentric coordinates one pixel downwards
			w1 = w1_row + b1;
			w2 = w2_row + b2;
			w3 = w3_row + b3;
		}

		if (isUniformColor) {
			SDL_RenderFillRects(CurrentDevice->Renderer, rectsbuffer, rects_i);
		}

		// Restore original texture color and alpha mod.
		if (texture) {
			SDL_SetTextureColorMod(texture, original_mod_r, original_mod_g, original_mod_b);
			SDL_SetTextureAlphaMod(texture, original_mod_a);
		}
	}
}

namespace ImGuiSDL
{
	static int ImGuiSDLEventWatch(void *userdata, SDL_Event *event) {
		if (event->type == SDL_RENDER_TARGETS_RESET) {
			CurrentDevice->CacheWasInvalidated = true;
		}
		return 0;
	}

	void Initialize(SDL_Renderer* renderer, int windowWidth, int windowHeight)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize.x = static_cast<float>(windowWidth);
		io.DisplaySize.y = static_cast<float>(windowHeight);

		ImGui::GetStyle().WindowRounding = 0.0f;
		ImGui::GetStyle().AntiAliasedFill = false;
		ImGui::GetStyle().AntiAliasedLines = false;
		ImGui::GetStyle().ChildRounding = 0.0f;
		ImGui::GetStyle().PopupRounding = 0.0f;
		ImGui::GetStyle().FrameRounding = 0.0f;
		ImGui::GetStyle().ScrollbarRounding = 0.0f;
		ImGui::GetStyle().GrabRounding = 0.0f;
		ImGui::GetStyle().TabRounding = 0.0f;

		// Loads the font texture.
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		static constexpr uint32_t rmask = 0x000000ff, gmask = 0x0000ff00, bmask = 0x00ff0000, amask = 0xff000000;
		SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(pixels, width, height, 32, 4 * width, rmask, gmask, bmask, amask);
		io.Fonts->TexID = SDL_CreateTextureFromSurface(renderer, surface);
		SDL_FreeSurface(surface);

		CurrentDevice = new Device(renderer);
		SDL_AddEventWatch(ImGuiSDLEventWatch, nullptr);
	}

	void Deinitialize()
	{
		// Frees up the memory of the font texture.
		ImGuiIO& io = ImGui::GetIO();
		SDL_Texture* texture = static_cast<SDL_Texture*>(io.Fonts->TexID);
		SDL_DestroyTexture(texture);

		delete CurrentDevice;
		SDL_DelEventWatch(ImGuiSDLEventWatch, nullptr);
	}

	void Render(ImDrawData* drawData)
	{
		if (CurrentDevice->CacheWasInvalidated) {
			CurrentDevice->CacheWasInvalidated = false;
			CurrentDevice->TriangleCache.Reset();
		}

		size_t num_triangles = 0;

		SDL_BlendMode blendMode;
		SDL_GetRenderDrawBlendMode(CurrentDevice->Renderer, &blendMode);
		SDL_SetRenderDrawBlendMode(CurrentDevice->Renderer, SDL_BLENDMODE_BLEND);

		Uint8 initialR, initialG, initialB, initialA;
		SDL_GetRenderDrawColor(CurrentDevice->Renderer, &initialR, &initialG, &initialB, &initialA);

		SDL_bool initialClipEnabled = SDL_RenderIsClipEnabled(CurrentDevice->Renderer);
		SDL_Rect initialClipRect;
		SDL_RenderGetClipRect(CurrentDevice->Renderer, &initialClipRect);

		SDL_Texture* initialRenderTarget = SDL_GetRenderTarget(CurrentDevice->Renderer);

		ImGuiIO& io = ImGui::GetIO();

		for (int n = 0; n < drawData->CmdListsCount; n++)
		{
			auto commandList = drawData->CmdLists[n];
			auto vertexBuffer = commandList->VtxBuffer;
			auto indexBuffer = commandList->IdxBuffer.Data;

			for (int cmd_i = 0; cmd_i < commandList->CmdBuffer.Size; cmd_i++)
			{
				const ImDrawCmd* drawCommand = &commandList->CmdBuffer[cmd_i];

				const Device::ClipRect clipRect = {
					static_cast<int>(drawCommand->ClipRect.x),
					static_cast<int>(drawCommand->ClipRect.y),
					static_cast<int>(drawCommand->ClipRect.z - drawCommand->ClipRect.x),
					static_cast<int>(drawCommand->ClipRect.w - drawCommand->ClipRect.y)
				};
				CurrentDevice->SetClipRect(clipRect);

				if (drawCommand->UserCallback)
				{
					drawCommand->UserCallback(commandList, drawCommand);
				}
				else
				{
					// Loops over triangles.
					for (unsigned int i = 0; i + 3 <= drawCommand->ElemCount; i += 3)
					{
						num_triangles++;
						ImDrawVert v0 = vertexBuffer[indexBuffer[i + 0]];
						ImDrawVert v1 = vertexBuffer[indexBuffer[i + 1]];
						ImDrawVert v2 = vertexBuffer[indexBuffer[i + 2]];

						const Rect bounding = Rect::CalculateBoundingBox(v0, v1, v2);
						const bool isTriangleUniformColor = v0.col == v1.col && v1.col == v2.col;
						const bool doesTriangleUseOnlyColor = bounding.UsesOnlyColor();

						if ((bounding.MinX > clipRect.X + clipRect.Width || bounding.MaxX < clipRect.X)
							&& (bounding.MinY > clipRect.Y + clipRect.Height || bounding.MaxY < clipRect.Y)
						) {
							// Not in clip rect, ignore
							continue;
						}

						SDL_Texture *texture = doesTriangleUseOnlyColor ? nullptr : (SDL_Texture*)drawCommand->TextureId;

						// First we check if there is a cached version of this triangle already waiting for us. If so, we can just do a super fast texture copy.
						v0.pos.x -= (int)bounding.MinX; v0.pos.y -= (int)bounding.MinY;
						v1.pos.x -= (int)bounding.MinX; v1.pos.y -= (int)bounding.MinY;
						v2.pos.x -= (int)bounding.MinX; v2.pos.y -= (int)bounding.MinY;

						const Device::GenericTriangleKey key = std::make_tuple(
							std::make_tuple(v0.pos.x, v0.pos.y, v0.uv.x, v0.uv.y, v0.col),
							std::make_tuple(v1.pos.x, v1.pos.y, v1.uv.x, v1.uv.y, v1.col),
							std::make_tuple(v2.pos.x, v2.pos.y, v2.uv.x, v2.uv.y, v2.col),
							texture
						);

						if (CurrentDevice->TriangleCache.Contains(key)) {
							const auto& cached = CurrentDevice->TriangleCache.At(key);
							const SDL_Rect destination = { (int)bounding.MinX, (int)bounding.MinY, (int)cached->Width, (int)cached->Height };
							SDL_RenderCopy(CurrentDevice->Renderer, cached->Texture, nullptr, &destination);
						} else {
							auto cached = std::make_unique<Device::TriangleCacheItem>();
							cached->Width = bounding.MaxX - bounding.MinX + 1;
							cached->Height = bounding.MaxY - bounding.MinY + 1;
							cached->Texture = CurrentDevice->MakeTexture(cached->Width, cached->Height);

							CurrentDevice->UseAsRenderTarget(cached->Texture);
							SDL_SetRenderDrawBlendMode(CurrentDevice->Renderer, SDL_BLENDMODE_NONE);
							DrawTriangle(v0, v1, v2, texture);
							CurrentDevice->UseAsRenderTarget(initialRenderTarget);
							SDL_SetRenderDrawBlendMode(CurrentDevice->Renderer, SDL_BLENDMODE_BLEND);

							const SDL_Rect destination = { (int)bounding.MinX, (int)bounding.MinY, (int)cached->Width, (int)cached->Height };
							SDL_RenderCopy(CurrentDevice->Renderer, cached->Texture, nullptr, &destination);

							CurrentDevice->TriangleCache.Insert(key, std::move(cached));
						}
					}
				}

				indexBuffer += drawCommand->ElemCount;
			}
		}

		CurrentDevice->DisableClip();

		SDL_SetRenderTarget(CurrentDevice->Renderer, initialRenderTarget);

		SDL_RenderSetClipRect(CurrentDevice->Renderer, initialClipEnabled ? &initialClipRect : nullptr);

		SDL_SetRenderDrawColor(CurrentDevice->Renderer,
			initialR, initialG, initialB, initialA);

		SDL_SetRenderDrawBlendMode(CurrentDevice->Renderer, blendMode);

		CurrentDevice->TriangleCache.SetCapacity(num_triangles);
		CurrentDevice->TriangleCache.Clean();
	}
}
