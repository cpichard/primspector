#pragma once

///
/// Declarations of Command classes only.
/// We want to have different implementations with or without undo/redo
///
struct PrimNew;
struct PrimRemove;
struct PrimChangeName;
struct PrimChangeSpecifier;
struct PrimAddReference;

struct EditorSelectPrimPath;

struct LayerRemoveSubLayer;
struct LayerInsertSubLayer;
struct LayerMoveSubLayer;

/// Post a command to be executed after the the UI has been drawn
template<typename CommandClass, typename... ArgTypes>
void DispatchCommand(ArgTypes... arguments);

/// Process the commands waiting. Only one command would be waiting at the moment
void ProcessCommands();