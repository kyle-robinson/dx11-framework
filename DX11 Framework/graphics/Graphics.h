#pragma once
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <map>
#include "Cube.h"
#include "Plane.h"
#include "Sprite.h"
#include "Shaders.h"
#include "Camera2D.h"
#include "ImGuiManager.h"
#include "RenderableGameObject.h"
#include <dxtk/SpriteFont.h>
#include <dxtk/SpriteBatch.h>
#include <dxtk/WICTextureLoader.h>

#define CUBE_AMOUNT 3

namespace Bind
{
	class Sampler;
	class Stencil;
	class Blender;
	class Viewport;
	class SwapChain;
	class Rasterizer;
	class DepthStencil;
	class RenderTarget;
}

class Graphics
{
	friend class GraphicsResource;
public:
	enum class GameState
	{
		MENU,
		PLAY,
		EDIT,
		HELP
	} gameState = GameState::MENU;

	virtual ~Graphics( void ) = default;
	bool Initialize( HWND hWnd, int width, int height );
	void BeginFrame();
	void RenderFrame();
	void EndFrame();
	void Update( float dt );
	UINT GetWidth() const noexcept { return windowWidth; }
	UINT GetHeight() const noexcept { return windowHeight; }

	Light light;
	int menuPage;
	Sprite circle;
	Sprite square;
	bool flyCamera = true;
	std::string cameraToUse = "Main";
	std::vector<RenderableGameObject> renderables;
	std::map<std::string, std::shared_ptr<Camera3D>> cameras;
	std::map<std::string, std::shared_ptr<Bind::Viewport>> viewports;
private:
	bool InitializeDirectX( HWND hWnd );
	bool InitializeShaders();
	bool InitializeScene();

	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> boxTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> grassTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> starsTexture;

	std::shared_ptr<Bind::Blender> blendState;
	std::shared_ptr<Bind::SwapChain> swapChain;
	std::shared_ptr<Bind::DepthStencil> depthStencil;
	std::shared_ptr<Bind::RenderTarget> backBuffer;
	std::shared_ptr<Bind::RenderTarget> renderTarget;
	std::map<std::string, std::shared_ptr<Bind::Sampler>> samplerStates;
	std::map<std::string, std::shared_ptr<Bind::Stencil>> stencilStates;
	std::map<std::string, std::shared_ptr<Bind::Rasterizer>> rasterizerStates;

	VertexShader vertexShader_2D;
	VertexShader vertexShader_full;
	VertexShader vertexShader_color;
	VertexShader vertexShader_light;
	VertexShader vertexShader_skybox;
	VertexShader vertexShader_noLight;

	PixelShader pixelShader_2D;
	PixelShader pixelShader_full;
	PixelShader pixelShader_color;
	PixelShader pixelShader_light;
	PixelShader pixelShader_skybox;
	PixelShader pixelShader_noLight;
	PixelShader pixelShader_2D_discard;

	ConstantBuffer<CB_VS_fog> cb_vs_fog;
	ConstantBuffer<CB_PS_scene> cb_ps_scene;
	ConstantBuffer<CB_PS_light> cb_ps_light;
	ConstantBuffer<CB_VS_matrix> cb_vs_matrix;
	ConstantBuffer<CB_PS_outline> cb_ps_outline;
	ConstantBuffer<CB_VS_matrix_2D> cb_vs_matrix_2d;
	ConstantBuffer<CB_VS_fullscreen> cb_vs_fullscreen;

	UINT windowWidth;
	UINT windowHeight;
	ImGuiManager imgui;

	Sprite menuBG;
	Sprite menuLogo;
	Sprite menuLight;
	Sprite menuScene;
	Sprite menuCamera;
	Camera2D camera2D;
	PlaneInstanced ground;
	PlaneFullscreen fullscreen;
	std::unique_ptr<Cube> skybox;
	std::unique_ptr<SpriteFont> spriteFont;
	std::unique_ptr<SpriteBatch> spriteBatch;
	std::vector<std::unique_ptr<Cube>> cubes;
};

#endif