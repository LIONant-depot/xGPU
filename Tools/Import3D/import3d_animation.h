namespace import3d
{
    struct bone_keyframes
    {
        std::vector<xcore::transform3>  m_Transfoms;        // Vector for all the frames of the animation
    };

    struct animation
    {
        std::string                     m_Name;             // Name for the animation (for debug)
        int                             m_FPS;              // What is the frame per second (ideally we should have 60fps or grader)
        float                           m_TimeLength;       // How long in seconds is this animation
        std::vector<bone_keyframes>     m_BoneKeyFrames;    // Keyframe used for the animation, note that the order of the keyframes in the vector should match the skeleton order
    };

    struct anim_package
    {
        std::vector<animation>          m_Animations;       // A list of animation
    };
}