import { Routes } from '@angular/router';
import { authGuard } from './services/auth.guard';
import { roleGuard } from './services/role.guard';

export const routes: Routes = [
  {
    path: '',
    loadComponent: () =>
      import('./pages/login/login').then((m) => m.Login)
  },
  {
    path: 'dashboard-root',
    loadComponent: () =>
      import('./pages/dashboard-root/dashboard-root').then((m) => m.DashboardRoot),
    canActivate: [authGuard, roleGuard(['root', 'admin_cliente'])]
  },
  {
    path: 'clientes',
    loadComponent: () =>
      import('./pages/clientes/clientes').then((m) => m.Clientes),
    canActivate: [authGuard, roleGuard(['root'])]
  },
  {
    path: 'usuarios',
    loadComponent: () =>
      import('./pages/usuarios/usuarios').then((m) => m.Usuarios),
    canActivate: [authGuard, roleGuard(['root', 'admin_cliente'])]
  },
  {
    path: 'PreCadastroDisp',
    loadComponent: () =>
      import('./pages/pre-cadastro-disp/pre-cadastro-disp').then((m) => m.PreCadastroDisp),
    canActivate: [authGuard, roleGuard(['root', 'admin_cliente'])]
  },
  {
    path: 'dispositivos',
    loadComponent: () =>
      import('./pages/dispositivos/dispositivos').then((m) => m.Dispositivos),
    canActivate: [authGuard, roleGuard(['root', 'admin_cliente'])]
  },
  {
    path: 'permissoes',
    loadComponent: () =>
      import('./pages/permissoes/permissoes').then((m) => m.Permissoes),
    canActivate: [authGuard, roleGuard(['root'])]
  }
];
