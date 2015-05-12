#include "pch.h"
#include "AlembicImporter.h"
#ifdef aiWindows
    #include <windows.h>
#endif // aiWindows

using namespace Alembic;

typedef std::shared_ptr<Abc::IArchive> abcArchivePtr;

#ifdef aiWithDebugLog
void aiDebugLog(const char* fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);

#ifdef aiWindows
    char buf[2048];
    vsprintf(buf, fmt, vl);
    ::OutputDebugStringA(buf);
#else // aiWindows
    vprintf(vl);
#endif // aiWindows

    va_end(vl);
}
#endif // aiWithDebugLog


class aiContext
{
public:
    static aiContextPtr create();
    static void destroy(aiContextPtr ctx);

    bool load(const char *path);
    abcObject* getTopObject();
    void setCurrentObject(abcObject *obj);
    void setCurrentTime(float time);

    const char* getName() const;
    const char* getFullName() const;
    uint32_t    getNumChildren() const;
    bool        hasXForm() const;
    bool        hasPolyMesh() const;
    abcV3       getPosition() const;
    abcV3       getRotation() const;
    abcV3       getScale() const;
    abcM44      getMatrix() const;
    uint32_t    getIndexCount() const;
    uint32_t    getVertexCount() const;
    void        copyIndices(int *dst, bool reverse);
    void        copyVertices(abcV3 *dst);
    void        copyNormals(abcV3 *dst);
    void        copyUVs(abcV2 *dst);

private:
    static std::vector<aiContextPtr> s_contexts;
    static std::mutex s_mutex;

    abcArchivePtr m_archive;
    abcObject m_top_object;

    abcObject m_current;
    AbcGeom::XformSample m_xf;
    AbcGeom::IPolyMeshSchema m_mesh_schema;
    Abc::ISampleSelector m_sample_selector;
    Abc::Int32ArraySamplePtr m_indices;
    Abc::Int32ArraySamplePtr m_counts;
    Abc::P3fArraySamplePtr m_positions;
    Abc::V3fArraySamplePtr m_velocities;
};

std::vector<aiContextPtr> aiContext::s_contexts;
std::mutex aiContext::s_mutex;

aiContextPtr aiContext::create()
{
    std::lock_guard<std::mutex> l(s_mutex);
    auto ctx = new aiContext();
    for (int i=0; ; ++i) {
        if (s_contexts.size() <= i) {
            s_contexts.push_back(ctx);
            break;
        }
        else if (!s_contexts[i]) {
            s_contexts[i] = ctx;
            break;
        }
    }

    return ctx;
}

void aiContext::destroy(aiContextPtr ctx)
{
    std::lock_guard<std::mutex> l(s_mutex);
    auto it = std::find(s_contexts.begin(), s_contexts.end(), ctx);
    if (it != s_contexts.end()) {
        s_contexts.erase(it);
        delete ctx;
    }
}


bool aiContext::load(const char *path)
{
    try {
        aiDebugLog("trying to open AbcCoreHDF5::ReadArchive...\n");
        m_archive = abcArchivePtr(new Abc::IArchive(AbcCoreHDF5::ReadArchive(), path));
    }
    catch (Alembic::Util::Exception e)
    {
        aiDebugLog("exception: %s\n", e.what());

        try {
            aiDebugLog("trying to open AbcCoreOgawa::ReadArchive...\n");
            m_archive = abcArchivePtr(new Abc::IArchive(AbcCoreOgawa::ReadArchive(), path));
        }
        catch (Alembic::Util::Exception e)
        {
            aiDebugLog("exception: %s\n", e.what());
        }
    }

    if (m_archive->valid()) {
        m_top_object = m_archive->getTop();
        aiDebugLog("succeeded\n");
        return true;
    }
    else {
        m_archive.reset();
        return false;
    }
}

abcObject* aiContext::getTopObject()
{
    return &m_top_object;
}

void aiContext::setCurrentObject(abcObject *obj)
{
    m_current = *obj;
    if (m_current.valid())
    {
        if (hasXForm())
        {
            AbcGeom::IXform x(*obj, Abc::kWrapExisting);
            x.getSchema().get(m_xf);
        }

        if (hasPolyMesh())
        {
            AbcGeom::IPolyMesh mesh(m_current.getParent(), m_current.getName());
            m_mesh_schema = mesh.getSchema();
            m_mesh_schema.getFaceIndicesProperty().get(m_indices, m_sample_selector);
            m_mesh_schema.getFaceCountsProperty().get(m_counts, m_sample_selector);
            m_mesh_schema.getPositionsProperty().get(m_positions, m_sample_selector);
            if (m_mesh_schema.getVelocitiesProperty().valid()) {
                m_mesh_schema.getVelocitiesProperty().get(m_velocities, m_sample_selector);
            }
            aiDebugLog("TopologyVariance: %d\n", m_mesh_schema.getTopologyVariance());
        }
    }
}

void aiContext::setCurrentTime(float time)
{
    m_sample_selector = Abc::ISampleSelector(time, Abc::ISampleSelector::kFloorIndex);
}

const char* aiContext::getName() const
{
    return m_current.getName().c_str();
}

const char* aiContext::getFullName() const
{
    return m_current.getFullName().c_str();
}

uint32_t aiContext::getNumChildren() const
{
    return m_current.getNumChildren();
}

bool aiContext::hasXForm() const
{
    return AbcGeom::IXformSchema::matches(m_current.getMetaData());
}

bool aiContext::hasPolyMesh() const
{
    return AbcGeom::IPolyMeshSchema::matches(m_current.getMetaData());
}

abcV3 aiContext::getPosition() const
{
    return abcV3(m_xf.getTranslation());
}

abcV3 aiContext::getRotation() const
{
    return abcV3(m_xf.getXRotation(), m_xf.getYRotation(), m_xf.getZRotation());
}

abcV3 aiContext::getScale() const
{
    return abcV3(m_xf.getScale());
}

abcM44 aiContext::getMatrix() const
{
    return abcM44(m_xf.getMatrix());
}

uint32_t aiContext::getIndexCount() const
{
    return m_indices->size();
}

uint32_t aiContext::getVertexCount() const
{
    return m_positions->size();
}

void aiContext::copyIndices(int *dst, bool reverse)
{
    const auto &cont = *m_indices;
    size_t n = cont.size();
    if (reverse) {
        for (size_t i = 0; i < n; ++i) {
            dst[i] = cont[n - i - 1];
        }
    }
    else {
        for (size_t i = 0; i < n; ++i) {
            dst[i] = cont[i];
        }
    }
}

void aiContext::copyVertices(abcV3 *dst)
{
    const auto &cont = *m_positions;
    size_t n = cont.size();
    for (size_t i = 0; i < n; ++i) {
        dst[i] = cont[i];
    }
}

void aiContext::copyNormals(abcV3 *dst)
{
}

void aiContext::copyUVs(abcV2 *dst)
{
}




aiCLinkage aiExport aiContextPtr aiCreateContext()
{
    auto ctx = aiContext::create();
    aiDebugLog("aiCreateContext(): %p\n", ctx);
    return ctx;
}

aiCLinkage aiExport void aiDestroyContext(aiContextPtr ctx)
{
    aiDebugLog("aiDestroyContext(): %p\n", ctx);
    aiContext::destroy(ctx);
}


aiCLinkage aiExport bool aiLoad(aiContextPtr ctx, const char *path)
{
    aiDebugLog("aiLoad(): %d %s\n", ctx, path);
    return ctx->load(path);
}

aiCLinkage aiExport abcObject* aiGetTopObject(aiContextPtr ctx)
{
    return ctx->getTopObject();
}

aiCLinkage aiExport void aiEnumerateChild(aiContextPtr ctx, abcObject *obj, aiNodeEnumerator e, void *userdata)
{
    aiDebugLog("aiEnumerateChild(): %d %s (%d children)\n", ctx, obj->getName().c_str(), obj->getNumChildren());
    size_t n = obj->getNumChildren();
    for (size_t i = 0; i < n; ++i) {
        try {
            abcObject child(*obj, obj->getChildHeader(i).getName());
            ctx->setCurrentObject(&child);
            e(ctx, &child, userdata);
        }
        catch (Alembic::Util::Exception e)
        {
            aiDebugLog("exception: %s\n", e.what());
        }
    }
}


aiCLinkage aiExport void aiSetCurrentObject(aiContextPtr ctx, abcObject *obj)
{
    ctx->setCurrentObject(obj);
}

aiCLinkage aiExport void aiSetCurrentTime(aiContextPtr ctx, float time)
{
    aiDebugLog("aiSetCurrentTime(): %.2f\n", time);
    ctx->setCurrentTime(time);
}


aiCLinkage aiExport const char* aiGetNameS(aiContextPtr ctx)
{
    return ctx->getName();
}

aiCLinkage aiExport const char* aiGetFullNameS(aiContextPtr ctx)
{
    return ctx->getFullName();
}

aiCLinkage aiExport uint32_t aiGetNumChildren(aiContextPtr ctx)
{
    return ctx->getNumChildren();
}

aiCLinkage aiExport bool aiHasXForm(aiContextPtr ctx)
{
    return ctx->hasXForm();
}

aiCLinkage aiExport bool aiHasPolyMesh(aiContextPtr ctx)
{
    return ctx->hasPolyMesh();
}

aiCLinkage aiExport aiV3 aiGetPosition(aiContextPtr ctx)
{
    return (aiV3&)ctx->getPosition();
}

aiCLinkage aiExport aiV3 aiGetRotation(aiContextPtr ctx)
{
    return (aiV3&)ctx->getRotation();
}

aiCLinkage aiExport aiV3 aiGetScale(aiContextPtr ctx)
{
    return (aiV3&)ctx->getScale();
}

aiCLinkage aiExport aiM44 aiGetMatrix(aiContextPtr ctx)
{
    return (aiM44&)ctx->getMatrix();
}

aiCLinkage aiExport uint32_t aiGetVertexCount(aiContextPtr ctx)
{
    return ctx->getVertexCount();
}

aiCLinkage aiExport uint32_t aiGetIndexCount(aiContextPtr ctx)
{
    return ctx->getIndexCount();
}

aiCLinkage aiExport void aiCopyIndices(aiContextPtr ctx, int *indices, bool reverse)
{
    ctx->copyIndices(indices, reverse);
}

aiCLinkage aiExport void aiCopyVertices(aiContextPtr ctx, abcV3 *vertices)
{
    ctx->copyVertices(vertices);
}

aiCLinkage aiExport void aiCopyNormals(aiContextPtr ctx, abcV3 *normals)
{
    ctx->copyNormals(normals);
}

aiCLinkage aiExport void aiCopyUVs(aiContextPtr ctx, abcV2 *uvs)
{
    ctx->copyUVs(uvs);
}