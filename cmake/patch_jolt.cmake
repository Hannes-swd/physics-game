# Korrektur für Jolt Physics 5.3.0 (wird beim Herunterladen automatisch angewendet, mehrfaches Ausführen schadet nicht):
# Prüft eine zusammengesetzte Form (Compound) gegen eine andere Form, werden die Teilformen nur dann getestet, wenn sich
# die Hüllquader wirklich überlappen. Der Abstand für vorausschauende Kontakte (mMaxSeparationDistance) fehlt dabei,
# anders als im umgekehrten Fall. Folge: Teilhüllen knapp über dem Boden bekommen keinen Kontakt, ein gebohrtes Brett
# landet nur auf einer Hälfte, bekommt Drall und taucht in den Boden ein.
set(file "Jolt/Physics/Collision/Shape/CompoundShapeVisitors.h")
file(READ "${file}" text)
string(FIND "${text}" "mBoundsOf2InSpaceOf1.ExpandBy" done)
if(done EQUAL -1)
  set(old "mBoundsOf2InSpaceOf1 = inShape2->GetLocalBounds().Scaled(inScale2).Transformed(transform2_to_1);")
  string(FIND "${text}" "${old}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "patch_jolt: Stelle in ${file} nicht gefunden (andere Jolt-Version?)")
  endif()
  string(REPLACE "${old}" "${old}\n\t\tmBoundsOf2InSpaceOf1.ExpandBy(Vec3::sReplicate(inCollideShapeSettings.mMaxSeparationDistance)); // Werkstatt-Patch" text "${text}")
  file(WRITE "${file}" "${text}")
  message(STATUS "patch_jolt: CompoundShapeVisitors.h korrigiert")
endif()

# Rückmeldung der Kontaktimpulse: Jolt merkt sich nach dem Geschwindigkeitslöser die Impulse jedes Kontaktpunkts nur
# intern (für den Warmstart). Das Spiel braucht sie für Druck in Bohrungen, Abrieb, Ausreißen und den Inspektor.
# Ein Funktionszeiger (Standard: keiner) wird für jeden Kontaktpunkt aufgerufen, aus mehreren Physik-Threads.
set(file "Jolt/Physics/Constraints/ContactConstraintManager.h")
file(READ "${file}" text)
string(FIND "${text}" "sContactImpulseCallback" done)
if(done EQUAL -1)
  set(old "	ContactListener *			GetContactListener() const											{ return mContactListener; }")
  string(FIND "${text}" "${old}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "patch_jolt: Stelle in ${file} nicht gefunden (andere Jolt-Version?)")
  endif()
  string(REPLACE "${old}" "${old}\n\n	/// Werkstatt-Patch: nach dem Geschwindigkeitslöser für jeden Kontaktpunkt aufgerufen (aus mehreren Threads).\n	/// inNormalImpulse wirkt auf Körper 2 entlang inNormal (von 1 nach 2), inFrictionImpulse ebenfalls auf Körper 2.\n	using ContactImpulseCallback = void (*)(const Body &inBody1, const Body &inBody2, RVec3Arg inPoint, Vec3Arg inNormal, float inNormalImpulse, Vec3Arg inFrictionImpulse);\n	static inline ContactImpulseCallback sContactImpulseCallback = nullptr;" text "${text}")
  file(WRITE "${file}" "${text}")
  message(STATUS "patch_jolt: ContactConstraintManager.h erweitert")
endif()

set(file "Jolt/Physics/Constraints/ContactConstraintManager.cpp")
file(READ "${file}" text)
string(FIND "${text}" "sContactImpulseCallback" done)
if(done EQUAL -1)
  set(old "			wcp.mContactPoint->mFrictionLambda[1] = wcp.mFrictionConstraint2.GetTotalLambda();\n")
  string(FIND "${text}" "${old}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "patch_jolt: Stelle in ${file} nicht gefunden (andere Jolt-Version?)")
  endif()
  string(REPLACE "${old}" "${old}			if (sContactImpulseCallback != nullptr) // Werkstatt-Patch\n			{\n				Vec3 t1, t2;\n				constraint.GetTangents(t1, t2);\n				RVec3 p = constraint.mBody1->GetCenterOfMassTransform() * Vec3::sLoadFloat3Unsafe(wcp.mContactPoint->mPosition1);\n				sContactImpulseCallback(*constraint.mBody1, *constraint.mBody2, p, constraint.GetWorldSpaceNormal(), wcp.mNonPenetrationConstraint.GetTotalLambda(), t1 * wcp.mFrictionConstraint1.GetTotalLambda() + t2 * wcp.mFrictionConstraint2.GetTotalLambda());\n			}\n" text "${text}")
  file(WRITE "${file}" "${text}")
  message(STATUS "patch_jolt: ContactConstraintManager.cpp erweitert")
endif()
