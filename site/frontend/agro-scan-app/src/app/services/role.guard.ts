import { CanActivateFn, Router } from '@angular/router';
import { inject } from '@angular/core';
import { AuthService } from './auth.service';

export const roleGuard = (allowedRoles: string[]): CanActivateFn => {
  return () => {
    const auth = inject(AuthService);
    const router = inject(Router);

    const role = auth.getRole();

    if (auth.isLoggedIn() && role && allowedRoles.includes(role)) {
      return true;
    }

    router.navigate(['/']);
    return false;
  };
};
