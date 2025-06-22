import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Router } from '@angular/router';
import { AuthService } from '../../services/auth.service';

@Component({
  selector: 'app-login',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './login.html',
  styleUrls: ['./login.css']
})
export class Login {
  email = '';
  password = '';
  loading = false;
  errorMessage = '';

  constructor(public authService: AuthService, private router: Router) {}

  ngOnInit(): void {
  if (this.authService.isLoggedIn()) {
    this.authService.loadCustomClaims();
  }
  }

  async login() {
    this.loading = true;
    this.errorMessage = '';

    try {
      await this.authService.login(this.email.trim(), this.password.trim());
      this.router.navigate(['/dashboard-root']);
    } catch (error) {
      console.error('Erro no login:', error);
      this.errorMessage = 'E-mail ou senha inválidos.';
    }

    this.loading = false;
  }
}
