#ifndef IViewModel_HPP_INCLUDED
#define IViewModel_HPP_INCLUDED
// <copyright file="IViewModel.hpp" company="3Dconnexion">
// ------------------------------------------------------------------------------------------------
// Copyright (c) 2022 3Dconnexion. All rights reserved.
//
// This file and source code are an integral part of the "3Dconnexion Software Developer Kit",
// including all accompanying documentation, and is protected by intellectual property laws. All
// use of the 3Dconnexion Software Developer Kit is subject to the License Agreement found in the
// "LicenseAgreementSDK.txt" file. All rights not expressly granted by 3Dconnexion are reserved.
// ------------------------------------------------------------------------------------------------
// </copyright>
// <history>
// ************************************************************************************************
// File History
//
// $Id$
//
// </history>

#include <iostream>

namespace TDx {
namespace ActionInput {
namespace View {
  enum Projection { Orthographic, Perspective };

/// <summary>
/// ViewModel interface used for the ActionInput sample
/// </summary>
/// <remarks>In the sample there is no real view model.</remarks>
class IViewModel {
public:
  virtual ~IViewModel() = default;

  /// <summary>
  /// Set the view projection.
  /// </summary>
  /// <param name="p">One of <see cref="Projection"/>.</param>
  virtual void PutProjection(Projection p) = 0;

  /// <summary>
  /// Select all obejcts.
  /// </summary>
  virtual void SelectAll() = 0;

  /// <summary>
  /// Clear the current selection.
  /// </summary>
  virtual void ClearSelection() = 0;
};

/// <summary>
/// The viewport view model implmentation.
/// </summary>
class CViewportViewModel : public IViewModel {
public:
  /// <inheritdoc/>
  void PutProjection(Projection p) override {
    switch (p)
    {
    case Orthographic:
      std::cout << "View set to orthographic projection\n";
      break;
    case Perspective:
      std::cout << "View set to perspective projection\n";
      break;
    }
  }

  /// <inheritdoc/>
  void SelectAll() override {
    std::cout << "Everything selected\n";
  }

  /// <inheritdoc/>
  void ClearSelection() override {
    std::cout << "Nothing selected\n";
  }
};
} // namespace View
} // namespace ActionInput
} // namespace TDx
#endif // IViewModel_HPP_INCLUDED