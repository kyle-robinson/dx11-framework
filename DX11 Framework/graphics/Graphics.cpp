#include "Graphics.h"
#include "Blender.h"
#include "Sampler.h"
#include "Stencil.h"
#include "Viewport.h"
#include "ModelData.h"
#include "SwapChain.h"
#include "Rasterizer.h"
#include "InputLayout.h"
#include "../resource.h"
#include "DepthStencil.h"
#include "RenderTarget.h"
#include "ObjectIndices.h"
#include "ObjectVertices.h"
#include "../utility/Structs.h"
#include "../utility/Collisions.h"
#include "../utility/Billboarding.h"
#include <fstream>

bool Graphics::Initialize( HWND hWnd, int width, int height )
{
	windowWidth = width;
	windowHeight = height;

	if ( !InitializeDirectX( hWnd ) )
        return false;

	if ( !InitializeShaders() )
        return false;

	if ( !InitializeScene() )
        return false;

    imgui.Initialize( hWnd, device.Get(), context.Get() );

	return true;
}

void Graphics::BeginFrame()
{
	// clear render target
    if ( ( viewportParams.useLeft && viewportParams.useSplit ) ||
        ( viewportParams.useFull && !viewportParams.useSplit ) ||
        ( sceneParams.multiView && !viewportParams.useSplit ) )
    {
        renderTarget->BindAsTexture( *this, depthStencil.get(), sceneParams.clearColor );
        depthStencil->ClearDepthStencil( *this );
    }

	// set render state
    sceneParams.rasterizerSolid ? rasterizerStates["Solid"]->Bind( *this ) : rasterizerStates["Wireframe"]->Bind( *this );
	context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    stencilStates["Off"]->Bind( *this );
    blendState->Bind( *this );

    // setup viewports
    if ( viewportParams.useFull )
        viewports["Full"]->Bind( *this );
    if ( viewportParams.useFull && viewportParams.useSplit )
        cameraToUse = viewportParams.controlLeftSide ? "Main" : "Point";
    if ( viewportParams.useLeft )
    {
        cameraToUse = "Main";
        viewportParams.useLeft = false;
        viewports["Left"]->Bind( *this );
    }
    if ( viewportParams.useRight )
    {
        cameraToUse = "Point";
        viewportParams.useRight = false;
        viewports["Right"]->Bind( *this );
    }

    // setup sampler
    if ( samplerParams.useAnisotropic )
        samplerStates["Anisotropic"]->Bind( *this );
    else if ( samplerParams.useBilinear )
        samplerStates["Bilinear"]->Bind( *this );
    else if ( samplerParams.usePoint )
        samplerStates["Point"]->Bind( *this );

    // setup constant buffers
    if ( !cb_vs_fog.ApplyChanges() ) return;
	context->VSSetConstantBuffers( 1, 1, cb_vs_fog.GetAddressOf() );
	context->PSSetConstantBuffers( 1, 1, cb_vs_fog.GetAddressOf() );

    cb_ps_light.data.useQuad = false;
    cb_ps_light.data.lightFlicker = lightParams.lightFlicker;
    cb_ps_light.data.flickerAmount = lightParams.flickerAmount;
    light.UpdateConstantBuffer( cb_ps_light );
	if ( !cb_ps_light.ApplyChanges() ) return;
	context->PSSetConstantBuffers( 2, 1, cb_ps_light.GetAddressOf() );

    cb_ps_scene.data.alphaFactor = sceneParams.alphaFactor;
    cb_ps_scene.data.useTexture = sceneParams.useTexture;
    if ( !cb_ps_scene.ApplyChanges() ) return;
	context->PSSetConstantBuffers( 3, 1, cb_ps_scene.GetAddressOf() );
}

void Graphics::RenderFrame()
{
    // setup sprite masking
    if ( sceneParams.useMask )
    {
        Shaders::BindShaders( context.Get(), vertexShader_2D, pixelShader_2D_discard );
        stencilStates["Mask"]->Bind( *this );
        sceneParams.circleMask ? circle.Draw( camera2D.GetWorldOrthoMatrix() ) : square.Draw( camera2D.GetWorldOrthoMatrix() );
        stencilStates["Write"]->Bind( *this );
    }

    // render models
    Shaders::BindShaders( context.Get(), vertexShader_light, pixelShader_light );
    for ( unsigned int i = 0; i < renderables.size(); i++ )
        renderables[i].Draw( cameras[cameraToUse]->GetViewMatrix(), cameras[cameraToUse]->GetProjectionMatrix() );

    // draw primitves
    for ( unsigned int i = 0; i < cubes.size(); i++ )
        cubes[i]->Draw( cb_vs_matrix, boxTexture.Get() );
    ground.DrawInstanced( cb_vs_matrix, cb_ps_light, grassTexture.Get() );

    // point light with outlining
    if ( lightParams.lightHover )
    {
        cb_ps_outline.data.outlineColor = outlineParams.outlineColor;
        if ( !cb_ps_outline.ApplyChanges() ) return;
	    context->PSSetConstantBuffers( 1, 1, cb_ps_outline.GetAddressOf() );

        stencilStates["Write"]->Bind( *this );
        light.Draw( cameras[cameraToUse]->GetViewMatrix(), cameras[cameraToUse]->GetProjectionMatrix() );

        Shaders::BindShaders( context.Get(), vertexShader_color, pixelShader_color );
        light.SetScale( outlineParams.outlineSize, outlineParams.outlineSize, outlineParams.outlineSize );
        stencilStates["Mask"]->Bind( *this );
        light.Draw( cameras[cameraToUse]->GetViewMatrix(), cameras[cameraToUse]->GetProjectionMatrix() );
    }

    Shaders::BindShaders( context.Get(), vertexShader_light, pixelShader_noLight );
    light.SetScale( 1.0f, 1.0f, 1.0f );
	light.Draw( cameras[cameraToUse]->GetViewMatrix(), cameras[cameraToUse]->GetProjectionMatrix() );

    // menu systems
    if ( gameState == GameState::MENU || gameState == GameState::HELP )
    {
        Shaders::BindShaders( context.Get(), vertexShader_2D, pixelShader_2D );

        cb_ps_scene.data.alphaFactor = 0.9f;
        cb_ps_scene.data.useTexture = false;
        if ( !cb_ps_scene.ApplyChanges() ) return;
	    context->PSSetConstantBuffers( 1, 1, cb_ps_scene.GetAddressOf() );
        menuBG.Draw( camera2D.GetWorldOrthoMatrix() );

        // render main menu
        if ( gameState == GameState::MENU )
        {
            cb_ps_scene.data.useTexture = true;
            if ( !cb_ps_scene.ApplyChanges() ) return;
	        context->PSSetConstantBuffers( 1, 1, cb_ps_scene.GetAddressOf() );
            menuLogo.Draw( camera2D.GetWorldOrthoMatrix() );
        }

        // render help menu
        if ( gameState == GameState::HELP )
        {
            cb_ps_scene.data.useTexture = true;
            if ( !cb_ps_scene.ApplyChanges() ) return;
	        context->PSSetConstantBuffers( 1, 1, cb_ps_scene.GetAddressOf() );
            switch ( menuPage )
            {
                case 0: menuCamera.Draw( camera2D.GetWorldOrthoMatrix() ); break;
                case 1: menuLight.Draw( camera2D.GetWorldOrthoMatrix() ); break;
                case 2: menuScene.Draw( camera2D.GetWorldOrthoMatrix() ); break;
            }
        }
    }

    // render cubemap
    if ( cb_ps_light.data.usePointLight )
    {
        Shaders::BindShaders( context.Get(), vertexShader_light, pixelShader_light );
        skybox->SetScale( 500.0f, 500.0f, 500.0f );
        skybox->SetPosition( cameras[cameraToUse]->GetPositionFloat3() );
        stencilStates["Off"]->Bind( *this );
        rasterizerStates["Cubemap"]->Bind( *this );
        skybox->Draw( cb_vs_matrix, starsTexture.Get() );
        sceneParams.rasterizerSolid ? rasterizerStates["Solid"]->Bind( *this ) : rasterizerStates["Wireframe"]->Bind( *this );
    }
}

void Graphics::EndFrame()
{
    // set and clear back buffer
    backBuffer->BindAsBuffer( *this, sceneParams.clearColor );

    // render to fullscreen texture
    fullscreen.SetupBuffers( vertexShader_full, pixelShader_full, cb_vs_fullscreen, sceneParams.multiView );
    context->PSSetShaderResources( 0, 1, renderTarget->GetShaderResourceViewPtr() );
    Bind::Rasterizer::DrawSolid( *this, fullscreen.ib_full.IndexCount() ); // always draw as solid

    // render text
    spriteBatch->Begin();
    static XMFLOAT2 fontPositionMode = { windowWidth - 350.0f, 0.0f };
    if ( gameState != GameState::MENU && gameState != GameState::HELP )
    {
        static XMFLOAT2 fontPositionLight;
        fontPositionLight = { windowWidth / 2.0f - 115.0f, windowHeight / 2.0f - 20.0f };
        fontPositionLight = viewportParams.useSplit ? XMFLOAT2( fontPositionLight.x / 2.0f - 50.0f, fontPositionLight.y ) : fontPositionLight;
        if ( lightParams.isEquippable && cameraToUse == "Main" && !lightParams.lightStuck )
            spriteFont->DrawString( spriteBatch.get(), L"Press 'C' to equip light.", fontPositionLight,
                Colors::White, 0.0f, XMFLOAT2( 0.0f, 0.0f ), XMFLOAT2( 1.0f, 1.0f ) );
        spriteFont->DrawString( spriteBatch.get(), L"Press 'F3' to view help menu.", XMFLOAT2( windowWidth / 2.0f - 150.0f, 0.0f  ),
            Colors::White, 0.0f, XMFLOAT2( 0.0f, 0.0f ), XMFLOAT2( 1.0f, 1.0f ) );
    }
    if ( gameState == GameState::PLAY )
        spriteFont->DrawString( spriteBatch.get(), L"Press 'F2' to switch to EDIT mode.", fontPositionMode,
            Colors::White, 0.0f, XMFLOAT2( 0.0f, 0.0f ), XMFLOAT2( 1.0f, 1.0f ) );
    if ( gameState == GameState::EDIT )
        spriteFont->DrawString( spriteBatch.get(), L"Press 'F1' to switch to PLAY mode.", fontPositionMode,
            Colors::White, 0.0f, XMFLOAT2( 0.0f, 0.0f ), XMFLOAT2( 1.0f, 1.0f ) );
    spriteBatch->End();

    // display imgui
    if ( gameState == GameState::EDIT )
    {
        imgui.BeginRender();
        imgui.RenderMainWindow( *this );
        if ( spawnWindow.sceneWindow ) imgui.RenderSceneWindow( *this );
        if ( spawnWindow.lightWindow ) imgui.RenderLightWindow( light, cb_ps_light );
        if ( spawnWindow.fogWindow ) imgui.RenderFogWindow( cb_vs_fog );
        if ( spawnWindow.modelWindow ) imgui.RenderModelWindow( renderables );
        if ( spawnWindow.cameraWindow ) imgui.RenderCameraWindow( *this, *cameras[cameraToUse], cameraToUse );
        if ( spawnWindow.stencilWindow ) imgui.RenderStencilWindow( *this );
        imgui.EndRender();
    }

    // unbind rtv and srv
    renderTarget->BindAsNull( *this );
    backBuffer->BindAsNull( *this );

    // display frame
	HRESULT hr = swapChain->GetSwapChain()->Present( 1, NULL );
	if ( FAILED( hr ) )
	{
		hr == DXGI_ERROR_DEVICE_REMOVED ?
            ErrorLogger::Log( device->GetDeviceRemovedReason(), "Swap Chain. Graphics device removed!" ) :
            ErrorLogger::Log( hr, "Swap Chain failed to render frame!" );
		exit( -1 );
	}
}

void Graphics::Update( float dt )
{
    // primitive transformations
    for ( unsigned int i = 0; i < cubes.size(); i++ )
        cubes[i]->AdjustRotation( 0.0f, 0.001f * dt, 0.0f );
    ground.UpdateInstanced( 5, 6, 8, 60 );

    // camera viewing
    Camera3D::UpdateThirdPerson( cameras["Third"], renderables[0] );
    Collisions::CheckCollision3D( cameras["Point"], renderables[0], 20.0f, 10.0f ) ?
        sceneParams.cameraCollision = true : sceneParams.cameraCollision = false;

    // nanosuit billboarding
    float rotation = Billboarding::BillboardModel( cameras[cameraToUse], renderables[0] );
    if ( sceneParams.useBillboarding && cameraToUse != "Third" )
        renderables[0].SetRotation( 0.0f, rotation, 0.0f );

    // point light equipping and flickering
    light.UpdatePhysics();
    light.UpdateFlicker( cb_ps_light );
    Collisions::CheckCollision3D( cameras["Main"], light, 5.0f ) ? lightParams.isEquippable = true : lightParams.isEquippable = false;
}

bool Graphics::InitializeDirectX( HWND hWnd )
{
    try
    {
        swapChain = std::make_shared<Bind::SwapChain>( *this, context.GetAddressOf(), device.GetAddressOf(), hWnd );
        backBuffer = std::make_shared<Bind::RenderTarget>( *this, swapChain->GetSwapChain() );
        renderTarget = std::make_shared<Bind::RenderTarget>( *this );

        depthStencil = std::make_shared<Bind::DepthStencil>( *this );
        blendState = std::make_shared<Bind::Blender>( *this );

        viewports.emplace( "Full", std::make_shared<Bind::Viewport>( *this, Bind::Viewport::Side::Full ) );
        viewports.emplace( "Left", std::make_shared<Bind::Viewport>( *this, Bind::Viewport::Side::Left ) );
        viewports.emplace( "Right", std::make_shared<Bind::Viewport>( *this, Bind::Viewport::Side::Right ) );

        stencilStates.emplace( "Off", std::make_shared<Bind::Stencil>( *this, Bind::Stencil::Mode::Off ) );
        stencilStates.emplace( "Mask", std::make_shared<Bind::Stencil>( *this, Bind::Stencil::Mode::Mask ) );
        stencilStates.emplace( "Write", std::make_shared<Bind::Stencil>( *this, Bind::Stencil::Mode::Write ) );

        rasterizerStates.emplace( "Solid", std::make_shared<Bind::Rasterizer>( *this, true, false ) );
        rasterizerStates.emplace( "Cubemap", std::make_shared<Bind::Rasterizer>( *this, true, true ) );
        rasterizerStates.emplace( "Wireframe", std::make_shared<Bind::Rasterizer>( *this, false, true ) );

        samplerStates.emplace( "Anisotropic", std::make_shared<Bind::Sampler>( *this, Bind::Sampler::Type::Anisotropic ) );
        samplerStates.emplace( "Bilinear", std::make_shared<Bind::Sampler>( *this, Bind::Sampler::Type::Bilinear ) );
        samplerStates.emplace( "Point", std::make_shared<Bind::Sampler>( *this, Bind::Sampler::Type::Point ) );

        spriteBatch = std::make_unique<SpriteBatch>( context.Get() );
        spriteFont = std::make_unique<SpriteFont>( device.Get(), L"res\\fonts\\open_sans_ms_16.spritefont" );
    }
    catch ( COMException& exception )
    {
        ErrorLogger::Log( exception );
        return false;
    }

    return true;
}

bool Graphics::InitializeShaders()
{
    try
    {
        /*   MODELS   */
        HRESULT hr = vertexShader_light.Initialize( device, L"res\\shaders\\Model.fx", IPL::layoutPosTexNrm, ARRAYSIZE( IPL::layoutPosTexNrm ) );
		COM_ERROR_IF_FAILED( hr, "Failed to create light vertex shader!" );
	    hr = pixelShader_light.Initialize( device, L"res\\shaders\\Model.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create light pixel shader!" );
	    hr = pixelShader_noLight.Initialize( device, L"res\\shaders\\Model_NoLight.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create no light pixel shader!" );

        hr = vertexShader_color.Initialize( device, L"res\\shaders\\Primitive.fx", IPL::layoutPosCol, ARRAYSIZE( IPL::layoutPosCol ) );
        COM_ERROR_IF_FAILED( hr, "Failed to create colour vertex shader!" );
        hr = pixelShader_color.Initialize( device, L"res\\shaders\\Primitive.fx" );
        COM_ERROR_IF_FAILED( hr, "Failed to create colour pixel shader!" );

        /*   SPRITES   */
	    hr = vertexShader_2D.Initialize( device, L"res\\shaders\\Sprite.fx", IPL::layoutPosTex, ARRAYSIZE( IPL::layoutPosTex ) );
		COM_ERROR_IF_FAILED( hr, "Failed to create 2D vertex shader!" );
	    hr = pixelShader_2D.Initialize( device, L"res\\shaders\\Sprite.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create 2D pixel shader!" );
        hr = pixelShader_2D_discard.Initialize( device, L"res\\shaders\\Sprite_Discard.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create 2D discard pixel shader!" );

        /*   POST-PROCESSING   */
	    hr = vertexShader_full.Initialize( device, L"res\\shaders\\Fullscreen.fx", IPL::layoutPos, ARRAYSIZE( IPL::layoutPos ) );
		COM_ERROR_IF_FAILED( hr, "Failed to create fullscreen vertex shader!" );
	    hr = pixelShader_full.Initialize( device, L"res\\shaders\\Fullscreen.fx" );
		COM_ERROR_IF_FAILED( hr, "Failed to create fullscreen pixel shader!" );
    }
    catch ( COMException& exception )
    {
        ErrorLogger::Log( exception );
        return false;
    }

	return true;
}

bool Graphics::InitializeScene()
{
    try
    {
        /*   MODELS   */
        if ( !ModelData::LoadModelData( "res\\objects.json" ) )
            return false;
        if ( !ModelData::InitializeModelData( context.Get(), device.Get(), cb_vs_matrix, renderables ) )
            return false;

        light.SetScale( 1.0f, 1.0f, 1.0f );
		if ( !light.Initialize( device.Get(), context.Get(), cb_vs_matrix ) )
			return false;

        /*   SPRITES   */
        if ( !menuBG.Initialize( device.Get(), context.Get(), windowWidth, windowHeight, "res\\textures\\Transparency.png", cb_vs_matrix_2d ) )
            return false;
        menuBG.SetInitialPosition( windowWidth / 2 - menuBG.GetWidth() / 2, windowHeight / 2 - menuBG.GetHeight() / 2, 0 );

        if ( !menuLogo.Initialize( device.Get(), context.Get(), windowWidth, windowHeight, "res\\textures\\dx-logo-new.png", cb_vs_matrix_2d ) )
            return false;
        menuLogo.SetInitialPosition( windowWidth / 2 - menuLogo.GetWidth() / 2, windowHeight / 2 - menuLogo.GetHeight() / 2, 0 );

        if ( !menuLight.Initialize( device.Get(), context.Get(), windowWidth, windowHeight, "res\\textures\\point-light.png", cb_vs_matrix_2d ) )
            return false;
        menuLight.SetInitialPosition( windowWidth / 2 - menuLight.GetWidth() / 2, windowHeight / 2 - menuLight.GetHeight() / 2, 0 );

        if ( !menuCamera.Initialize( device.Get(), context.Get(), windowWidth, windowHeight, "res\\textures\\camera.png", cb_vs_matrix_2d ) )
            return false;
        menuCamera.SetInitialPosition( windowWidth / 2 - menuCamera.GetWidth() / 2, windowHeight / 2 - menuCamera.GetHeight() / 2, 0 );

        if ( !menuScene.Initialize( device.Get(), context.Get(), windowWidth, windowHeight, "res\\textures\\scene.png", cb_vs_matrix_2d ) )
            return false;
        menuScene.SetInitialPosition( windowWidth / 2 - menuScene.GetWidth() / 2, windowHeight / 2 - menuScene.GetHeight() / 2, 0 );

        if ( !circle.Initialize( device.Get(), context.Get(), 256, 256, "res\\textures\\circle.png", cb_vs_matrix_2d ) )
            return false;
        circle.SetInitialPosition( windowWidth / 2 - circle.GetWidth() / 2, windowHeight / 2 - circle.GetHeight() / 2, 0 );

        if ( !square.Initialize( device.Get(), context.Get(), 256, 256, "res\\textures\\purpleheart.png", cb_vs_matrix_2d ) )
            return false;
        square.SetInitialPosition( windowWidth / 2 - square.GetWidth() / 2, windowHeight / 2 - square.GetHeight() / 2, 0 );

        /*   OBJECTS   */
        XMFLOAT2 aspectRatio = { static_cast<float>( windowWidth ), static_cast<float>( windowHeight ) };
        camera2D.SetProjectionValues( aspectRatio.x, aspectRatio.y, 0.0f, 1.0f );

        cameras.emplace( "Main", std::make_shared<Camera3D>( 0.0f, 9.0f, -20.0f ) );
        cameras["Main"]->SetProjectionValues( 70.0f, aspectRatio.x / aspectRatio.y, 0.1f, 1000.0f );

        cameras.emplace( "Point", std::make_shared<Camera3D>( 0.0f, 9.0f, -55.0f ) );
        cameras["Point"]->SetProjectionValues( 70.0f, aspectRatio.x / aspectRatio.y, 0.1f, 1000.0f );

        cameras.emplace( "Third", std::make_shared<Camera3D>( renderables[0].GetPositionFloat3() ) );
        cameras["Third"]->SetProjectionValues( 70.0f, aspectRatio.x / aspectRatio.y, 0.1f, 1000.0f );

        XMVECTOR lightPosition = cameras["Main"]->GetPositionVector() + cameras["Main"]->GetForwardVector();
		light.SetPosition( XMVectorGetX( lightPosition ), 5.25f, XMVectorGetZ( lightPosition ) + 5.0f );
		light.SetRotation( cameras["Main"]->GetRotationFloat3() );

        /*   VERTEX/INDEX   */
        for ( unsigned int i = 0; i < CUBE_AMOUNT; i++ )
        {
            std::unique_ptr<Cube> cube = std::make_unique<Cube>( context.Get(), device.Get() );
            cube->SetInitialPosition( -5.0f + ( i * 5.0f ), 9.0f, 0.0f );
            cubes.push_back( std::move( cube ) );
        }

        skybox = std::make_unique<Cube>( context.Get(), device.Get() );

        if ( !fullscreen.Initialize( context.Get(), device.Get() ) )
            return false;

        if ( !ground.InitializeInstanced( context.Get(), device.Get(), 400 ) )
            return false;

        /*   TEXTURES   */
        HRESULT hr = CreateWICTextureFromFile( device.Get(), L"res\\textures\\CrashBox.png", nullptr, boxTexture.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create box texture from file!" );

        hr = CreateWICTextureFromFile( device.Get(), L"res\\textures\\grass.jpg", nullptr, grassTexture.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create grass texture from file!" );

        hr = CreateWICTextureFromFile( device.Get(), L"res\\textures\\stars.jpg", nullptr, starsTexture.GetAddressOf() );
        COM_ERROR_IF_FAILED( hr, "Failed to create stars texture from file!" );

        /*   CONSTANT BUFFERS   */
        hr = cb_vs_fog.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_vs_fog' Constant Buffer!" );
        cb_vs_fog.data.fogColor = { 0.2f, 0.2f, 0.2f };
        cb_vs_fog.data.fogStart = 10.0f;
        cb_vs_fog.data.fogEnd = 50.0f;
        cb_vs_fog.data.fogEnable = false;

        hr = cb_vs_matrix.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_vs_matrix' Constant Buffer!" );

        hr = cb_vs_matrix_2d.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_vs_matrix_2d' Constant Buffer!" );

        hr = cb_vs_fullscreen.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_vs_fullscreen' Constant Buffer!" );

		hr = cb_ps_light.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_ps_light' Constant Buffer!" );

        hr = cb_ps_scene.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_ps_scene' Constant Buffer!" );

        hr = cb_ps_outline.Initialize( device.Get(), context.Get() );
		COM_ERROR_IF_FAILED( hr, "Failed to initialize 'cb_ps_ouline' Constant Buffer!" );
    }
    catch ( COMException& exception )
    {
        ErrorLogger::Log( exception );
        return false;
    }
    return true;
}