#pragma once
#include "rc_man.h"


TINKER_NAMESPACE_BEGIN
/// \ingroup md
/// \brief Individual molecules in current system.
struct Molecule
{
   /// \brief Total number of separate molecules in the system.
   int nmol;
   /// \brief First and last atom of each molecule in the list.
   int (*imol)[2];
   /// \brief Contiguous list of the atoms in each molecule.
   int* kmol;
   /// \brief Number of the molecule to which each atom belongs.
   int* molecule;
   /// \brief Total weight of all the molecules in the system.
   real totmass;
   /// \brief Molecular weight for each molecule in the system.
   real* molmass;


   ~Molecule();
};
TINKER_EXTERN Molecule molecule;


void molecule_data(rc_op);
TINKER_NAMESPACE_END