namespace xgpu::windows
{
    //----------------------------------------------------------------------------------

    bool instance::ProcessInputEvents( void ) noexcept
    {
        //
        // Clear all input data before processing
        //
        m_Mouse->m_ButtonIndex = 1 - m_Mouse->m_ButtonIndex;

        for (int i = 0; i < static_cast<int>(xgpu::mouse::digital::ENUM_COUNT); i++)
        {
            m_Mouse->m_ButtonWasDown[m_Mouse->m_ButtonIndex][i] = 0;
        }

        m_Mouse->m_Analog[(int)xgpu::mouse::analog::POS_REL][0] = 0;
        m_Mouse->m_Analog[(int)xgpu::mouse::analog::POS_REL][1] = 0;
        m_Mouse->m_Analog[(int)xgpu::mouse::analog::WHEEL_REL][0] = 0;
        m_Mouse->m_Analog[(int)xgpu::mouse::analog::WHEEL_REL][1] = 0;

        m_Keyboard->m_KeyWasDownIndex = 1 - m_Keyboard->m_KeyWasDownIndex;

        for (int i = 0; i < static_cast<int>(xgpu::keyboard::digital::ENUM_COUNT); i++)
        {
            m_Keyboard->m_KeyWasDown[m_Keyboard->m_KeyWasDownIndex][i] = 0;
        }

        //
        // Update all the window messages
        //
        bool bContinue = true;
        {
            MSG Message;
            while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            {
                if (WM_QUIT == Message.message)
                {
                    bContinue = false;
                    break;
                }
                else
                {
                    TranslateMessage(&Message);
                    DispatchMessageW(&Message);
                }
            }
        }

        return bContinue;
    }
}