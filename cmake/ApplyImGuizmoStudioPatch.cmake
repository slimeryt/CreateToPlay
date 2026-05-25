# Patches fetched ImGuizmo for CreateToPlay Studio gizmo layout.
function(apply_imguizmo_studio_patch imguizmo_source_dir)
    set(ig_cpp "${imguizmo_source_dir}/src/ImGuizmo.cpp")
    set(ig_h  "${imguizmo_source_dir}/src/ImGuizmo.h")
    if(NOT EXISTS "${ig_cpp}")
        message(WARNING "ImGuizmo.cpp not found; skipping studio gizmo patch")
        return()
    endif()

    file(READ "${ig_cpp}" ig_content)

    set(planes_old
"         // draw plane
         if (!gContext.mbUsing || (gContext.mbUsing && type == MT_MOVE_YZ + i))
         {
            if (belowPlaneLimit && Contains(op, TRANSLATE_PLANS[i]))
            {
               ImVec2 screenQuadPts[4];
               for (int j = 0; j < 4; ++j)
               {
                  vec_t cornerWorldPos = (dirPlaneX * quadUV[j * 2] + dirPlaneY * quadUV[j * 2 + 1]) * gContext.mScreenFactor;
                  screenQuadPts[j] = worldToPos(cornerWorldPos, gContext.mMVP);
               }
               drawList->AddPolyline(screenQuadPts, 4, GetColorU32(DIRECTION_X + i), 1.0f, ImDrawFlags_Closed);
               drawList->AddConvexPolyFilled(screenQuadPts, 4, colors[i + 4]);
            }
         }")
    set(planes_new "         // draw plane (disabled — Studio uses axis arrows only)")

    set(scale_center_old
"      // draw screen cirle
      drawList->AddCircleFilled(gContext.mScreenSquareCenter, gContext.mStyle.CenterCircleSize, colors[0], 32);")
    set(scale_center_new "      // draw screen circle (disabled — Studio uses per-axis scale balls only)")

    set(scale_screen_pick_old
"      // screen
      if (io.MousePos.x >= gContext.mScreenSquareMin.x && io.MousePos.x <= gContext.mScreenSquareMax.x &&
         io.MousePos.y >= gContext.mScreenSquareMin.y && io.MousePos.y <= gContext.mScreenSquareMax.y &&
         Contains(op, SCALE))
      {
         type = MT_SCALE_XYZ;
      }")
    set(scale_screen_pick_new
"      // screen uniform scale (disabled for Studio)
      if (false &&
         io.MousePos.x >= gContext.mScreenSquareMin.x && io.MousePos.x <= gContext.mScreenSquareMax.x &&
         io.MousePos.y >= gContext.mScreenSquareMin.y && io.MousePos.y <= gContext.mScreenSquareMax.y &&
         Contains(op, SCALE))
      {
         type = MT_SCALE_XYZ;
      }")

    if(NOT ig_content MATCHES "Studio uses axis arrows only")
        string(REPLACE "${planes_old}" "${planes_new}" ig_content "${ig_content}")
    endif()
    if(NOT ig_content MATCHES "per-axis scale balls only")
        string(REPLACE "${scale_center_old}" "${scale_center_new}" ig_content "${ig_content}")
        string(REPLACE "${scale_screen_pick_old}" "${scale_screen_pick_new}" ig_content "${ig_content}")
        string(REPLACE "drawList->AddCircleFilled(worldDirSSpace, gContext.mStyle.ScaleLineCircleSize, colors[i + 1]);"
                         "drawList->AddNgonFilled(worldDirSSpace, gContext.mStyle.ScaleLineCircleSize, colors[i + 1], 12);"
                         ig_content "${ig_content}")
        string(REPLACE "drawList->AddCircleFilled(worldDirSSpaceNoScale, gContext.mStyle.ScaleLineCircleSize, scaleLineColor);"
                         "drawList->AddNgonFilled(worldDirSSpaceNoScale, gContext.mStyle.ScaleLineCircleSize, scaleLineColor, 12);"
                         ig_content "${ig_content}")
    endif()

    # Bidirectional scale handles: x+/x-, y+/y-, z+/z-
    if(NOT ig_content MATCHES "Studio bidirectional scale handles")
        string(REPLACE
"      float mSaveMousePosx;

      // save axis factor when using gizmo"
"      float mSaveMousePosx;
      int mScaleHandleSign = 1;

      // save axis factor when using gizmo"
            ig_content "${ig_content}")

        set(scale_draw_old_ngon
"            // draw axis
            if (belowAxisLimit)
            {
               bool hasTranslateOnAxis = Contains(op, static_cast<OPERATION>(TRANSLATE_X << i));
               float markerScale = hasTranslateOnAxis ? 1.4f : 1.0f;
               ImVec2 baseSSpace = worldToPos(dirAxis * 0.1f * gContext.mScreenFactor, gContext.mMVP);
               ImVec2 worldDirSSpaceNoScale = worldToPos(dirAxis * markerScale * gContext.mScreenFactor, gContext.mMVP);
               ImVec2 worldDirSSpace = worldToPos((dirAxis * markerScale * scaleDisplay[i]) * gContext.mScreenFactor, gContext.mMVP);

               if (gContext.mbUsing && (gContext.GetCurrentID() == gContext.mEditingID))
               {
                  ImU32 scaleLineColor = GetColorU32(SCALE_LINE);
                  drawList->AddLine(baseSSpace, worldDirSSpaceNoScale, scaleLineColor, gContext.mStyle.ScaleLineThickness);
                  drawList->AddNgonFilled(worldDirSSpaceNoScale, gContext.mStyle.ScaleLineCircleSize, scaleLineColor, 12);
               }

               if (!hasTranslateOnAxis || gContext.mbUsing)
               {
                  drawList->AddLine(baseSSpace, worldDirSSpace, colors[i + 1], gContext.mStyle.ScaleLineThickness);
               }
               drawList->AddNgonFilled(worldDirSSpace, gContext.mStyle.ScaleLineCircleSize, colors[i + 1], 12);

               if (gContext.mAxisFactor[i] < 0.f)
               {
                  DrawHatchedAxis(dirAxis * scaleDisplay[i]);
               }
            }")

        set(scale_draw_new
"            // draw axis — Studio bidirectional scale handles (+/- per axis)
            if (belowAxisLimit)
            {
               bool hasTranslateOnAxis = Contains(op, static_cast<OPERATION>(TRANSLATE_X << i));
               float markerScale = hasTranslateOnAxis ? 1.4f : 1.0f;
               const float sc = scaleDisplay[i];
               ImU32 axisColor = colors[i + 1];
               vec_t axisNeg = dirAxis * -1.f;

               auto studioDrawScaleEnd = [&](const vec_t& axisVec, ImU32 color) {
                  ImVec2 baseSSpace = worldToPos(axisVec * 0.1f * gContext.mScreenFactor, gContext.mMVP);
                  ImVec2 endSSpace = worldToPos(axisVec * markerScale * sc * gContext.mScreenFactor, gContext.mMVP);
                  if (!hasTranslateOnAxis || gContext.mbUsing)
                     drawList->AddLine(baseSSpace, endSSpace, color, gContext.mStyle.ScaleLineThickness);
                  drawList->AddNgonFilled(endSSpace, gContext.mStyle.ScaleLineCircleSize, color, 12);
               };

               if (gContext.mbUsing && (gContext.GetCurrentID() == gContext.mEditingID))
               {
                  ImU32 scaleLineColor = GetColorU32(SCALE_LINE);
                  studioDrawScaleEnd(dirAxis, scaleLineColor);
                  studioDrawScaleEnd(axisNeg, scaleLineColor);
               }
               else
               {
                  studioDrawScaleEnd(dirAxis, axisColor);
                  studioDrawScaleEnd(axisNeg, axisColor);
               }

               if (gContext.mAxisFactor[i] < 0.f)
                  DrawHatchedAxis(dirAxis * scaleDisplay[i]);
            }")

        string(REPLACE "${scale_draw_old_ngon}" "${scale_draw_new}" ig_content "${ig_content}")

        set(scale_pick_loop_old
"         ComputeTripodAxisAndVisibility(i, dirAxis, dirPlaneX, dirPlaneY, belowAxisLimit, belowPlaneLimit, true);

         // draw axis
         if (belowAxisLimit)
         {
            bool hasTranslateOnAxis = Contains(op, static_cast<OPERATION>(TRANSLATE_X << i));
            float markerScale = hasTranslateOnAxis ? 1.4f : 1.0f;
            //ImVec2 baseSSpace = worldToPos(dirAxis * 0.1f * gContext.mScreenFactor, gContext.mMVPLocal);
            //ImVec2 worldDirSSpaceNoScale = worldToPos(dirAxis * markerScale * gContext.mScreenFactor, gContext.mMVP);
            ImVec2 worldDirSSpace = worldToPos((dirAxis * markerScale) * gContext.mScreenFactor, gContext.mMVPLocal);

            float distance = sqrtf(ImLengthSqr(worldDirSSpace - io.MousePos));
            if (distance < 12.f)
            {
               type = static_cast<MOVETYPE>(MT_SCALE_X + i);
            }
         }")

        set(scale_pick_loop_new
"         ComputeTripodAxisAndVisibility(i, dirAxis, dirPlaneX, dirPlaneY, belowAxisLimit, belowPlaneLimit, true);
         vec_t axisNeg = dirAxis * -1.f;

         if (belowAxisLimit)
         {
            bool hasTranslateOnAxis = Contains(op, static_cast<OPERATION>(TRANSLATE_X << i));
            float markerScale = hasTranslateOnAxis ? 1.4f : 1.0f;
            ImVec2 worldDirPos = worldToPos((dirAxis * markerScale) * gContext.mScreenFactor, gContext.mMVPLocal);
            ImVec2 worldDirNeg = worldToPos((axisNeg * markerScale) * gContext.mScreenFactor, gContext.mMVPLocal);
            float distPos = sqrtf(ImLengthSqr(worldDirPos - io.MousePos));
            float distNeg = sqrtf(ImLengthSqr(worldDirNeg - io.MousePos));
            const float pickR = 12.f;
            if (distPos < pickR || distNeg < pickR)
            {
               gContext.mScaleHandleSign = (distNeg < distPos) ? -1 : 1;
               type = static_cast<MOVETYPE>(MT_SCALE_X + i);
            }
         }")

        string(REPLACE "${scale_pick_loop_old}" "${scale_pick_loop_new}" ig_content "${ig_content}")

        set(scale_seg_pick_old
"         if ((closestPointOnAxis - makeVect(posOnPlanScreen)).Length() < 12.f) // pixel size
         {
            if (!isAxisMasked)
               type = static_cast<MOVETYPE>(MT_SCALE_X + i);
         }")

        set(scale_seg_pick_new
"         const ImVec2 axisStartNeg = worldToPos(gContext.mModelLocal.v.position - dirAxis * gContext.mScreenFactor * startOffset, gContext.mViewProjection);
         const ImVec2 axisEndNeg = worldToPos(gContext.mModelLocal.v.position - dirAxis * gContext.mScreenFactor * endOffset, gContext.mViewProjection);
         vec_t closestNeg = PointOnSegment(makeVect(posOnPlanScreen), makeVect(axisStartNeg), makeVect(axisEndNeg));
         float distPos = (closestPointOnAxis - makeVect(posOnPlanScreen)).Length();
         float distNeg = (closestNeg - makeVect(posOnPlanScreen)).Length();
         if (distPos < 12.f || distNeg < 12.f)
         {
            if (!isAxisMasked)
            {
               gContext.mScaleHandleSign = (distNeg < distPos) ? -1 : 1;
               type = static_cast<MOVETYPE>(MT_SCALE_X + i);
            }
         }")

        string(REPLACE "${scale_seg_pick_old}" "${scale_seg_pick_new}" ig_content "${ig_content}")

        string(REPLACE
"   void SetGizmoSizeClipSpace(float value)
   {
      gContext.mGizmoSizeClipSpace = value;
   }"
"   int GetScaleHandleSign()
   {
      return gContext.mScaleHandleSign;
   }

   void SetGizmoSizeClipSpace(float value)
   {
      gContext.mGizmoSizeClipSpace = value;
   }"
            ig_content "${ig_content}")
    endif()

    file(WRITE "${ig_cpp}" "${ig_content}")

    if(EXISTS "${ig_h}")
        file(READ "${ig_h}" ig_h_content)
        if(NOT ig_h_content MATCHES "GetScaleHandleSign")
            string(REPLACE
"   IMGUI_API MOVETYPE GetHoveredMoveType();

   // Allow axis to flip"
"   IMGUI_API MOVETYPE GetHoveredMoveType();
   // +1 = positive axis end, -1 = negative end (Studio bidirectional scale)
   IMGUI_API int GetScaleHandleSign();

   // Allow axis to flip"
                ig_h_content "${ig_h_content}")
            file(WRITE "${ig_h}" "${ig_h_content}")
        endif()
    endif()
endfunction()
