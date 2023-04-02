namespace import3d
{
    struct skeleton
    {
        struct bone
        {
            std::string    m_Name;               // Bone name used for debugging
            xcore::matrix4 m_InvBind;            // Skin/Bind Pose to Local Space of the bone
            xcore::matrix4 m_NeutalPose;         // The neutral pose given by the Nodes
            int            m_iParent;            // Parent used to to concadenate the matrices
        };

        int findBone(std::string_view BoneName) const
        {
            int i=0;
            for( auto& B : m_Bones ) if( BoneName == B.m_Name ) return i; else ++i;
            return -1;
        }

        std::vector<bone>   m_Bones;              // Bones are shorted (Parents go first)
    };
}
