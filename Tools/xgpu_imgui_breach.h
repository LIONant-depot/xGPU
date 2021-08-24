namespace xgpu::tools::imgui
{
    xgpu::device::error* CreateInstance( xgpu::instance& Intance, xgpu::device& Device, xgpu::window& MainWindow ) noexcept;

    void Shutdown   ( void ) noexcept;
    void NewFrame   ( void ) noexcept;
    void Render     ( void ) noexcept;
}