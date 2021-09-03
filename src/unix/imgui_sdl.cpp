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

	template <typename Key, typename Value, std::size_t Size> class LRUCache
	{
	public:
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

			Clean();
		}
	private:
		void Clean()
		{
			while (Container.size() > Size)
			{
				auto last = Order.end();
				last--;
				Container.erase(last->first);
				Order.pop_back();
			}
		}

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

		// You can tweak these to values that you find that work the best.
		static constexpr std::size_t UniformColorTriangleCacheSize = 512;
		static constexpr std::size_t GenericTriangleCacheSize = 64;

		// Uniform color is identified by its color and the coordinates of the edges.
		using UniformColorTriangleKey = std::tuple<uint32_t, int, int, int, int, int, int>;
		// The generic triangle cache unfortunately has to be basically a full representation of the triangle.
		// This includes the (offset) vertex positions, texture coordinates and vertex colors.
		using GenericTriangleVertexKey = std::tuple<int, int, double, double, uint32_t>;
		using GenericTriangleKey = std::tuple<GenericTriangleVertexKey, GenericTriangleVertexKey, GenericTriangleVertexKey>;

		LRUCache<UniformColorTriangleKey, std::unique_ptr<TriangleCacheItem>, UniformColorTriangleCacheSize> UniformColorTriangleCache;
		LRUCache<GenericTriangleKey, std::unique_ptr<TriangleCacheItem>, GenericTriangleCacheSize> GenericTriangleCache;

		Device(SDL_Renderer* renderer) : Renderer(renderer) { }

		void SetClipRect(const ClipRect& rect)
		{
			Clip = rect;
			const SDL_Rect clip = { rect.X, rect.Y, rect.Width, rect.Height };
			SDL_RenderSetClipRect(Renderer, &clip);
		}

		void EnableClip() { SetClipRect(Clip); }
		void DisableClip() { SDL_RenderSetClipRect(Renderer, nullptr); }

		void SetAt(int x, int y, const Color& color)
		{
			color.UseAsDrawColor(Renderer);
			SDL_RenderDrawPoint(Renderer, x, y);
		}

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

	struct Texture
	{
		SDL_Surface* Surface;
		SDL_Texture* Source;

		~Texture()
		{
			SDL_FreeSurface(Surface);
			SDL_DestroyTexture(Source);
		}

		Color Sample(float u, float v) const
		{
			const int x = static_cast<int>(std::round(u * (Surface->w - 1) + 0.5f));
			const int y = static_cast<int>(std::round(v * (Surface->h - 1) + 0.5f));

			const int location = y * Surface->w + x;
			assert(location < Surface->w * Surface->h);

			return Color(static_cast<uint32_t*>(Surface->pixels)[location]);
		}
	};

	template <typename T> class InterpolatedFactorEquation
	{
	public:
		InterpolatedFactorEquation(const T& value0, const T& value1, const T& value2, const ImVec2& v0, const ImVec2& v1, const ImVec2& v2)
			: Value0(value0), Value1(value1), Value2(value2), V0(v0), V1(v1), V2(v2),
			Divisor((V1.y - V2.y) * (V0.x - V2.x) + (V2.x - V1.x) * (V0.y - V2.y)) { }

		T Evaluate(float x, float y) const
		{
			const float w1 = ((V1.y - V2.y) * (x - V2.x) + (V2.x - V1.x) * (y - V2.y)) / Divisor;
			const float w2 = ((V2.y - V0.y) * (x - V2.x) + (V0.x - V2.x) * (y - V2.y)) / Divisor;
			const float w3 = 1.0f - w1 - w2;

			return static_cast<T>((Value0 * w1) + (Value1 * w2) + (Value2 * w3));
		}
	private:
		const T Value0;
		const T Value1;
		const T Value2;

		const ImVec2& V0;
		const ImVec2& V1;
		const ImVec2& V2;

		const float Divisor;
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

	struct FixedPointTriangleRenderInfo
	{
		int X1, X2, X3, Y1, Y2, Y3;
		int MinX, MaxX, MinY, MaxY;

		static FixedPointTriangleRenderInfo CalculateFixedPointTriangleInfo(const ImVec2& v1, const ImVec2& v2, const ImVec2& v3)
		{
			static constexpr float scale = 16.0f;

			const int x1 = static_cast<int>(std::round(v1.x * scale));
			const int x2 = static_cast<int>(std::round(v2.x * scale));
			const int x3 = static_cast<int>(std::round(v3.x * scale));

			const int y1 = static_cast<int>(std::round(v1.y * scale));
			const int y2 = static_cast<int>(std::round(v2.y * scale));
			const int y3 = static_cast<int>(std::round(v3.y * scale));

			int minX = (std::min({ x1, x2, x3 }) + 0xF) >> 4;
			int maxX = (std::max({ x1, x2, x3 }) + 0xF) >> 4;
			int minY = (std::min({ y1, y2, y3 }) + 0xF) >> 4;
			int maxY = (std::max({ y1, y2, y3 }) + 0xF) >> 4;

			return FixedPointTriangleRenderInfo{ x1, x2, x3, y1, y2, y3, minX, maxX, minY, maxY };
		}
	};

	void DrawTriangleWithColorFunction(const FixedPointTriangleRenderInfo& renderInfo, const std::function<Color(float x, float y)>& colorFunction, Device::TriangleCacheItem* cacheItem)
	{
		// Implementation source: https://web.archive.org/web/20171128164608/http://forum.devmaster.net/t/advanced-rasterization/6145.
		// This is a fixed point implementation that rounds to top-left.

		const int deltaX12 = renderInfo.X1 - renderInfo.X2;
		const int deltaX23 = renderInfo.X2 - renderInfo.X3;
		const int deltaX31 = renderInfo.X3 - renderInfo.X1;

		const int deltaY12 = renderInfo.Y1 - renderInfo.Y2;
		const int deltaY23 = renderInfo.Y2 - renderInfo.Y3;
		const int deltaY31 = renderInfo.Y3 - renderInfo.Y1;

		const int fixedDeltaX12 = deltaX12 << 4;
		const int fixedDeltaX23 = deltaX23 << 4;
		const int fixedDeltaX31 = deltaX31 << 4;

		const int fixedDeltaY12 = deltaY12 << 4;
		const int fixedDeltaY23 = deltaY23 << 4;
		const int fixedDeltaY31 = deltaY31 << 4;

		const int width = renderInfo.MaxX - renderInfo.MinX;
		const int height = renderInfo.MaxY - renderInfo.MinY;
		if (width == 0 || height == 0) return;

		int c1 = deltaY12 * renderInfo.X1 - deltaX12 * renderInfo.Y1;
		int c2 = deltaY23 * renderInfo.X2 - deltaX23 * renderInfo.Y2;
		int c3 = deltaY31 * renderInfo.X3 - deltaX31 * renderInfo.Y3;

		if (deltaY12 < 0 || (deltaY12 == 0 && deltaX12 > 0)) c1++;
		if (deltaY23 < 0 || (deltaY23 == 0 && deltaX23 > 0)) c2++;
		if (deltaY31 < 0 || (deltaY31 == 0 && deltaX31 > 0)) c3++;

		int edgeStart1 = c1 + deltaX12 * (renderInfo.MinY << 4) - deltaY12 * (renderInfo.MinX << 4);
		int edgeStart2 = c2 + deltaX23 * (renderInfo.MinY << 4) - deltaY23 * (renderInfo.MinX << 4);
		int edgeStart3 = c3 + deltaX31 * (renderInfo.MinY << 4) - deltaY31 * (renderInfo.MinX << 4);

		SDL_Texture* cache = CurrentDevice->MakeTexture(width, height);
		CurrentDevice->DisableClip();
		CurrentDevice->UseAsRenderTarget(cache);

		for (int y = renderInfo.MinY; y < renderInfo.MaxY; y++)
		{
			int edge1 = edgeStart1;
			int edge2 = edgeStart2;
			int edge3 = edgeStart3;

			for (int x = renderInfo.MinX; x < renderInfo.MaxX; x++)
			{
				if (edge1 > 0 && edge2 > 0 && edge3 > 0)
				{
					CurrentDevice->SetAt(x - renderInfo.MinX, y - renderInfo.MinY, colorFunction(x + 0.5f, y + 0.5f));
				}

				edge1 -= fixedDeltaY12;
				edge2 -= fixedDeltaY23;
				edge3 -= fixedDeltaY31;
			}

			edgeStart1 += fixedDeltaX12;
			edgeStart2 += fixedDeltaX23;
			edgeStart3 += fixedDeltaX31;
		}

		CurrentDevice->UseAsRenderTarget(nullptr);
		CurrentDevice->EnableClip();

		cacheItem->Texture = cache;
		cacheItem->Width = width;
		cacheItem->Height = height;
	}

	void DrawCachedTriangle(const Device::TriangleCacheItem& triangle, const FixedPointTriangleRenderInfo& renderInfo)
	{
		const SDL_Rect destination = { renderInfo.MinX, renderInfo.MinY, triangle.Width, triangle.Height };
		SDL_RenderCopy(CurrentDevice->Renderer, triangle.Texture, nullptr, &destination);
	}

	void DrawTriangle(const ImDrawVert& v1, const ImDrawVert& v2, const ImDrawVert& v3, const Texture* texture)
	{
		// The naming inconsistency in the parameters is intentional. The fixed point algorithm wants the vertices in a counter clockwise order.
		const auto& renderInfo = FixedPointTriangleRenderInfo::CalculateFixedPointTriangleInfo(v3.pos, v2.pos, v1.pos);

		// First we check if there is a cached version of this triangle already waiting for us. If so, we can just do a super fast texture copy.

		const auto key = std::make_tuple(
			std::make_tuple(static_cast<int>(std::round(v1.pos.x)) - renderInfo.MinX, static_cast<int>(std::round(v1.pos.y)) - renderInfo.MinY, v1.uv.x, v1.uv.y, v1.col),
			std::make_tuple(static_cast<int>(std::round(v2.pos.x)) - renderInfo.MinX, static_cast<int>(std::round(v2.pos.y)) - renderInfo.MinY, v2.uv.x, v2.uv.y, v2.col),
			std::make_tuple(static_cast<int>(std::round(v3.pos.x)) - renderInfo.MinX, static_cast<int>(std::round(v3.pos.y)) - renderInfo.MinY, v3.uv.x, v3.uv.y, v3.col));

		if (CurrentDevice->GenericTriangleCache.Contains(key))
		{
			const auto& cached = CurrentDevice->GenericTriangleCache.At(key);
			DrawCachedTriangle(*cached, renderInfo);

			return;
		}

		const InterpolatedFactorEquation<float> textureU(v1.uv.x, v2.uv.x, v3.uv.x, v1.pos, v2.pos, v3.pos);
		const InterpolatedFactorEquation<float> textureV(v1.uv.y, v2.uv.y, v3.uv.y, v1.pos, v2.pos, v3.pos);

		const InterpolatedFactorEquation<Color> shadeColor(Color(v1.col), Color(v2.col), Color(v3.col), v1.pos, v2.pos, v3.pos);

		auto cached = std::make_unique<Device::TriangleCacheItem>();
		DrawTriangleWithColorFunction(renderInfo, [&](float x, float y) {
			const float u = textureU.Evaluate(x, y);
			const float v = textureV.Evaluate(x, y);
			const Color sampled = texture->Sample(u, v);
			const Color shade = shadeColor.Evaluate(x, y);

			return sampled * shade;
		}, cached.get());

		if (!cached->Texture) return;

		const SDL_Rect destination = { renderInfo.MinX, renderInfo.MinY, cached->Width, cached->Height };
		SDL_RenderCopy(CurrentDevice->Renderer, cached->Texture, nullptr, &destination);

		CurrentDevice->GenericTriangleCache.Insert(key, std::move(cached));
	}

	void DrawUniformColorTriangle(const ImDrawVert& v1, const ImDrawVert& v2, const ImDrawVert& v3)
	{
		const Color color(v1.col);

		// The naming inconsistency in the parameters is intentional. The fixed point algorithm wants the vertices in a counter clockwise order.
		const auto& renderInfo = FixedPointTriangleRenderInfo::CalculateFixedPointTriangleInfo(v3.pos, v2.pos, v1.pos);

		const auto key =std::make_tuple(v1.col,
			static_cast<int>(std::round(v1.pos.x)) - renderInfo.MinX, static_cast<int>(std::round(v1.pos.y)) - renderInfo.MinY,
			static_cast<int>(std::round(v2.pos.x)) - renderInfo.MinX, static_cast<int>(std::round(v2.pos.y)) - renderInfo.MinY,
			static_cast<int>(std::round(v3.pos.x)) - renderInfo.MinX, static_cast<int>(std::round(v3.pos.y)) - renderInfo.MinY);
		if (CurrentDevice->UniformColorTriangleCache.Contains(key))
		{
			const auto& cached = CurrentDevice->UniformColorTriangleCache.At(key);
			DrawCachedTriangle(*cached, renderInfo);

			return;
		}

		auto cached = std::make_unique<Device::TriangleCacheItem>();
		DrawTriangleWithColorFunction(renderInfo, [&color](float, float) { return color; }, cached.get());

		if (!cached->Texture) return;

		const SDL_Rect destination = { renderInfo.MinX, renderInfo.MinY, cached->Width, cached->Height };
		SDL_RenderCopy(CurrentDevice->Renderer, cached->Texture, nullptr, &destination);

		CurrentDevice->UniformColorTriangleCache.Insert(key, std::move(cached));
	}

	void DrawRectangle(const Rect& bounding, SDL_Texture* texture, int textureWidth, int textureHeight, const Color& color, bool doHorizontalFlip, bool doVerticalFlip)
	{
		// We are safe to assume uniform color here, because the caller checks it and and uses the triangle renderer to render those.

		const SDL_Rect destination = {
			static_cast<int>(bounding.MinX),
			static_cast<int>(bounding.MinY),
			static_cast<int>(bounding.MaxX - bounding.MinX),
			static_cast<int>(bounding.MaxY - bounding.MinY)
		};

		// If the area isn't textured, we can just draw a rectangle with the correct color.
		if (bounding.UsesOnlyColor())
		{
			color.UseAsDrawColor(CurrentDevice->Renderer);
			SDL_RenderFillRect(CurrentDevice->Renderer, &destination);
		}
		else
		{
			// We can now just calculate the correct source rectangle and draw it.

			const SDL_Rect source = {
				static_cast<int>(bounding.MinU * textureWidth),
				static_cast<int>(bounding.MinV * textureHeight),
				static_cast<int>((bounding.MaxU - bounding.MinU) * textureWidth),
				static_cast<int>((bounding.MaxV - bounding.MinV) * textureHeight)
			};

			const SDL_RendererFlip flip = static_cast<SDL_RendererFlip>((doHorizontalFlip ? SDL_FLIP_HORIZONTAL : 0) | (doVerticalFlip ? SDL_FLIP_VERTICAL : 0));

			SDL_SetTextureColorMod(texture, static_cast<uint8_t>(color.R * 255), static_cast<uint8_t>(color.G * 255), static_cast<uint8_t>(color.B * 255));
			SDL_RenderCopyEx(CurrentDevice->Renderer, texture, &source, &destination, 0.0, nullptr, flip);
		}
	}

	void DrawRectangle(const Rect& bounding, const Texture* texture, const Color& color, bool doHorizontalFlip, bool doVerticalFlip)
	{
		DrawRectangle(bounding, texture->Source, texture->Surface->w, texture->Surface->h, color, doHorizontalFlip, doVerticalFlip);
	}

	void DrawRectangle(const Rect& bounding, SDL_Texture* texture, const Color& color, bool doHorizontalFlip, bool doVerticalFlip)
	{
		int width, height;
		SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
		DrawRectangle(bounding, texture, width, height, color, doHorizontalFlip, doVerticalFlip);
	}
}

namespace ImGuiSDL
{
	void Initialize(SDL_Renderer* renderer, int windowWidth, int windowHeight)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize.x = static_cast<float>(windowWidth);
		io.DisplaySize.y = static_cast<float>(windowHeight);

		ImGui::GetStyle().WindowRounding = 0.0f;
		ImGui::GetStyle().AntiAliasedFill = false;
		ImGui::GetStyle().AntiAliasedLines = false;

		// Loads the font texture.
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		static constexpr uint32_t rmask = 0x000000ff, gmask = 0x0000ff00, bmask = 0x00ff0000, amask = 0xff000000;
		SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(pixels, width, height, 32, 4 * width, rmask, gmask, bmask, amask);

		Texture* texture = new Texture();
		texture->Surface = surface;
		texture->Source = SDL_CreateTextureFromSurface(renderer, surface);
		io.Fonts->TexID = (void*)texture;

		CurrentDevice = new Device(renderer);
	}

	void Deinitialize()
	{
		// Frees up the memory of the font texture.
		ImGuiIO& io = ImGui::GetIO();
		Texture* texture = static_cast<Texture*>(io.Fonts->TexID);
		delete texture;

		delete CurrentDevice;
	}

	void Render(ImDrawData* drawData)
	{
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
					const bool isWrappedTexture = drawCommand->TextureId == io.Fonts->TexID;

					// Loops over triangles.
					for (unsigned int i = 0; i + 3 <= drawCommand->ElemCount; i += 3)
					{
						const ImDrawVert& v0 = vertexBuffer[indexBuffer[i + 0]];
						const ImDrawVert& v1 = vertexBuffer[indexBuffer[i + 1]];
						const ImDrawVert& v2 = vertexBuffer[indexBuffer[i + 2]];

						const Rect& bounding = Rect::CalculateBoundingBox(v0, v1, v2);

						const bool isTriangleUniformColor = v0.col == v1.col && v1.col == v2.col;
						const bool doesTriangleUseOnlyColor = bounding.UsesOnlyColor();

						// Actually, since we render a whole bunch of rectangles, we try to first detect those, and render them more efficiently.
						// How are rectangles detected? It's actually pretty simple: If all 6 vertices lie on the extremes of the bounding box,
						// it's a rectangle.
						if (i + 6 <= drawCommand->ElemCount)
						{
							const ImDrawVert& v3 = vertexBuffer[indexBuffer[i + 3]];
							const ImDrawVert& v4 = vertexBuffer[indexBuffer[i + 4]];
							const ImDrawVert& v5 = vertexBuffer[indexBuffer[i + 5]];

							const bool isUniformColor = isTriangleUniformColor && v2.col == v3.col && v3.col == v4.col && v4.col == v5.col;

							if (isUniformColor
							&& bounding.IsOnExtreme(v0.pos)
							&& bounding.IsOnExtreme(v1.pos)
							&& bounding.IsOnExtreme(v2.pos)
							&& bounding.IsOnExtreme(v3.pos)
							&& bounding.IsOnExtreme(v4.pos)
							&& bounding.IsOnExtreme(v5.pos))
							{
								// ImGui gives the triangles in a nice order: the first vertex happens to be the topleft corner of our rectangle.
								// We need to check for the orientation of the texture, as I believe in theory ImGui could feed us a flipped texture,
								// so that the larger texture coordinates are at topleft instead of bottomright.
								// We don't consider equal texture coordinates to require a flip, as then the rectangle is mostlikely simply a colored rectangle.
								const bool doHorizontalFlip = v2.uv.x < v0.uv.x;
								const bool doVerticalFlip = v2.uv.x < v0.uv.x;

								if (isWrappedTexture)
								{
									DrawRectangle(bounding, static_cast<const Texture*>(drawCommand->TextureId), Color(v0.col), doHorizontalFlip, doVerticalFlip);
								}
								else
								{
									DrawRectangle(bounding, static_cast<SDL_Texture*>(drawCommand->TextureId), Color(v0.col), doHorizontalFlip, doVerticalFlip);
								}

								i += 3;  // Additional increment to account for the extra 3 vertices we consumed.
								continue;
							}
						}

						if (isTriangleUniformColor && doesTriangleUseOnlyColor)
						{
							DrawUniformColorTriangle(v0, v1, v2);
						}
						else
						{
							// Currently we assume that any non rectangular texture samples the font texture. Dunno if that's what actually happens, but it seems to work.
							assert(isWrappedTexture);
							DrawTriangle(v0, v1, v2, static_cast<const Texture*>(drawCommand->TextureId));
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
	}
}
