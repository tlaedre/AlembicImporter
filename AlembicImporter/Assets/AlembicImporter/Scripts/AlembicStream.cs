using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Reflection;
using UnityEngine;
#if UNITY_EDITOR
using UnityEditor;
#endif

[ExecuteInEditMode]
public class AlembicStream : MonoBehaviour
{
    public enum CycleType { Hold, Loop, Reverse, Bounce };

    public string m_pathToAbc;
    public float m_time;
    public float m_startTime = 0.0f;
    public float m_endTime = 0.0f;
    public float m_timeOffset = 0.0f;
    public float m_timeScale = 1.0f;
    public bool m_preserveStartTime = true;
    public CycleType m_cycle = CycleType.Hold;
    public bool m_swapHandedness;
    public bool m_swapFaceWinding;
    public AlembicImporter.aiNormalMode m_normalMode = AlembicImporter.aiNormalMode.ComputeIfMissing;
    public bool m_enableTangents;
    public AlembicImporter.aiAspectRatioMode m_aspectRatioMode = AlembicImporter.aiAspectRatioMode.CurrentResolution;
    public bool m_ignoreMissingNodes = true;
    public bool m_forceRefresh;
    public bool m_verbose = false;
    public bool m_logToFile = false;
    public string m_logPath = "";
    
    bool m_loaded;
    float m_lastAdjustedTime;
    bool m_lastSwapHandedness;
    bool m_lastSwapFaceWinding;
    AlembicImporter.aiNormalMode m_lastNormalMode;
    bool m_lastEnableTangents;
    float m_timeEps = 0.001f;
    AlembicImporter.aiContext m_abc;
    bool m_lastIgnoreMissingNodes;
    float m_lastAspectRatio = -1.0f;


    void OnEnable()
    {
        m_abc = AlembicImporter.aiCreateContext();
        m_loaded = AlembicImporter.aiLoad(m_abc, m_pathToAbc);
    }

    void OnDisable()
    {
        AlembicImporter.aiDestroyContext(m_abc);
    }

    float AdjustTime(float inTime)
    {
        float extraOffset = 0.0f;

        // compute extra time offset to counter-balance effect of m_timeScale on m_startTime
        if (m_preserveStartTime)
        {
            extraOffset = m_startTime * (m_timeScale - 1.0f);
        }

        float playTime = m_endTime - m_startTime;

        // apply speed and offset
        float outTime = m_timeScale * (inTime - m_timeOffset) - extraOffset;

        if (m_cycle == CycleType.Hold)
        {
            if (outTime < (m_startTime - m_timeEps))
            {
                outTime = m_startTime;
            }
            else if (outTime > (m_endTime + m_timeEps))
            {
                outTime = m_endTime;
            }
        }
        else
        {
            float normalizedTime = (outTime - m_startTime) / playTime;
            float playRepeat = (float)Math.Floor(normalizedTime);
            float fraction = Math.Abs(normalizedTime - playRepeat);
            
            if (m_cycle == CycleType.Reverse)
            {
                if (outTime > (m_startTime + m_timeEps) && outTime < (m_endTime - m_timeEps))
                {
                    // inside alembic sample range
                    outTime = m_endTime - fraction * playTime;
                }
                else if (outTime < (m_startTime + m_timeEps))
                {
                    outTime = m_endTime;
                }
                else
                {
                    outTime = m_startTime;
                }
            }
            else
            {
                if (outTime < (m_startTime - m_timeEps) || outTime > (m_endTime + m_timeEps))
                {
                    // outside alembic sample range
                    if (m_cycle == CycleType.Loop || ((int)playRepeat % 2) == 0)
                    {
                        outTime = m_startTime + fraction * playTime;
                    }
                    else
                    {
                        outTime = m_endTime - fraction * playTime;
                    }
                }
            }
        }

        return outTime;
    }

    void Start()
    {
        m_time = 0.0f;

        m_lastAdjustedTime = AdjustTime(0.0f);
        m_lastSwapHandedness = m_swapHandedness;
        m_lastSwapFaceWinding = m_swapFaceWinding;
        m_lastNormalMode = m_normalMode;
        m_lastIgnoreMissingNodes = m_ignoreMissingNodes;
        m_lastEnableTangents = m_enableTangents;
        m_forceRefresh = true;
    }

    void UpdateAbc(float time)
    {
        AlembicImporter.aiEnableFileLog(m_logToFile, m_logPath);
        
        if (!m_loaded)
        {
            if (m_verbose)
            {
                Debug.Log("Load alembic '" + Application.streamingAssetsPath + "/" + m_pathToAbc);
            }
            m_loaded = AlembicImporter.aiLoad(m_abc, Application.streamingAssetsPath + "/" + m_pathToAbc);
        }

        if (m_loaded)
        {
            m_time = time;

            float adjustedTime = AdjustTime(m_time);
            float aspectRatio = AlembicImporter.GetAspectRatio(m_aspectRatioMode);

            if (m_forceRefresh || 
                m_swapHandedness != m_lastSwapHandedness ||
                m_swapFaceWinding != m_lastSwapFaceWinding ||
                m_normalMode != m_lastNormalMode ||
                m_ignoreMissingNodes != m_lastIgnoreMissingNodes ||
                m_enableTangents != m_lastEnableTangents ||
                Math.Abs(adjustedTime - m_lastAdjustedTime) > m_timeEps ||
                aspectRatio != m_lastAspectRatio)
            {
                if (m_verbose)
                {
                    Debug.Log("Update alembic at t=" + m_time + " (t'=" + adjustedTime + ")");
                }
                
                AlembicImporter.UpdateAbcTree(m_abc, GetComponent<Transform>(), adjustedTime,
                                              m_swapHandedness, m_swapFaceWinding,
                                              m_normalMode, m_enableTangents, aspectRatio,
                                              m_ignoreMissingNodes);
                
                m_lastAdjustedTime = adjustedTime;
                m_lastSwapHandedness = m_swapHandedness;
                m_lastSwapFaceWinding = m_swapFaceWinding;
                m_lastNormalMode = m_normalMode;
                m_lastAspectRatio = aspectRatio;
                m_lastEnableTangents = m_enableTangents;
                m_forceRefresh = false;
            }
            else
            {
                if (m_verbose)
                {
                    Debug.Log("No need to update alembic at t=" + m_time + " (t'=" + adjustedTime + ")");
                }
            }
        }
    }

    void Update()
    {
        if (Application.isPlaying)
        {
            UpdateAbc(Time.time);
        }
        else
        {
            UpdateAbc(m_time);
        }
    }
}
